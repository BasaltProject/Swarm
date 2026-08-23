package org.basaltmc.swarm

import io.ktor.network.selector.SelectorManager
import io.ktor.network.sockets.aSocket
import io.ktor.network.sockets.openReadChannel
import io.ktor.network.sockets.openWriteChannel
import io.ktor.utils.io.ByteReadChannel
import io.ktor.utils.io.ByteWriteChannel
import io.ktor.utils.io.readPacket
import kotlinx.io.Buffer
import kotlinx.io.Source
import kotlinx.io.readByteArray
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.delay
import kotlin.random.Random
import kotlin.math.sin
import kotlin.math.cos

enum class ProtocolState {
    LOGIN,
    CONFIGURATION,
    PLAY
}

class Bot(
    private val id: Int,
    private val host: String,
    private val port: Int,
    private val physicsEnabled: Boolean = false,
    private val target: SpawnTarget? = null,
    /** Which version this bot pretends to be. Everything version-shaped comes from here. */
    private val profile: ProtocolProfile = Protocols.newest,
) {
    private var state = ProtocolState.LOGIN
    private var compressionThreshold = -1
    private lateinit var sendChannel: ByteWriteChannel

    // Physics state
    private var x: Double = 0.0
    private var y: Double = 0.0
    private var z: Double = 0.0
    private var yaw: Float = 0f
    private var pitch: Float = 0f
    private var yVelocity: Double = 0.0
    
    // Wandering state
    private var isWandering: Boolean = false
    private var wanderTicks: Int = 0
    private var onGround: Boolean = false
    private var positionReceived: Boolean = false

    suspend fun connect() = kotlinx.coroutines.coroutineScope {
        val selectorManager = SelectorManager(Dispatchers.Default)
        try {
            val socket = aSocket(selectorManager).tcp().connect(host, port)
            val receiveChannel = socket.openReadChannel()
            sendChannel = socket.openWriteChannel(autoFlush = true)

            println("Bot $id connected to $host:$port")

            sendHandshakeAndLogin()

            while (true) {
                val length = receiveChannel.readVarInt()
                val rawPacketData = receiveChannel.readPacket(length)
                
                val packetData: Source
                if (compressionThreshold >= 0) {
                    val dataLength = rawPacketData.readVarInt()
                    if (dataLength == 0) {
                        packetData = rawPacketData
                    } else {
                        val compressedData = rawPacketData.readByteArray()
                        val decompressed = Zlib.decompress(compressedData, dataLength)
                        packetData = Buffer().apply { write(decompressed) }
                    }
                } else {
                    packetData = rawPacketData
                }

                val packetId = packetData.readVarInt()
                val continueLoop = handlePacket(packetId, packetData, this)
                if (!continueLoop) {
                    break
                }
            }
            
        } catch (e: Exception) {
            println("Bot $id disconnected or failed to connect: ${e.message}")
        }
    }

    private suspend fun sendHandshakeAndLogin() {
        sendChannel.writePacket(0x00, -1) {
            writeVarInt(profile.protocol)
            writeString(host)
            writeShort(port.toShort())
            writeVarInt(2) // Next state: Login
        }

        sendChannel.writePacket(0x00, -1) {
            writeString("Bot_$id")
            val uuidBytes = Random.nextBytes(16)
            uuidBytes[6] = (uuidBytes[6].toInt() and 0x0f or 0x40).toByte()
            uuidBytes[8] = (uuidBytes[8].toInt() and 0x3f or 0x80).toByte()
            write(uuidBytes)
        }
        println("Bot $id sent Login Start")
    }

    private suspend fun handlePacket(
        packetId: Int,
        packetData: Source,
        scope: kotlinx.coroutines.CoroutineScope,
    ): Boolean {
        return when (state) {
            ProtocolState.LOGIN -> handleLoginPacket(packetId, packetData, scope)
            ProtocolState.CONFIGURATION -> handleConfigurationPacket(packetId, packetData, scope)
            ProtocolState.PLAY -> handlePlayPacket(packetId, packetData)
        }
    }

    private suspend fun handleLoginPacket(
        packetId: Int,
        packetData: Source,
        scope: kotlinx.coroutines.CoroutineScope,
    ): Boolean {
        when (packetId) {
            profile.login.clientboundDisconnect -> {
                val reason = packetData.readByteArray().decodeToString()
                println("Bot $id disconnected in Login. Reason: $reason")
                return false
            }
            profile.login.clientboundFinished -> {
                packetData.readByteArray() // discard payload
                if (!profile.hasConfigurationPhase) {
                    // Before 1.20.2 there is nothing to acknowledge: the game has already started.
                    println("Bot $id received Login Success. Already playing.")
                    state = ProtocolState.PLAY
                    sendClientInformation()
                    if (physicsEnabled) {
                        scope.launchPhysicsLoop()
                    }
                    return true
                }
                println("Bot $id received Login Success. Sending Acknowledged.")
                sendChannel.writePacket(profile.login.serverboundAcknowledged, compressionThreshold) {}
                state = ProtocolState.CONFIGURATION
                sendClientInformation()
            }
            profile.login.clientboundCompression -> {
                compressionThreshold = packetData.readVarInt()
                println("Bot $id enabled compression. Threshold: $compressionThreshold")
            }
            else -> {
                println("Bot $id received unknown packet in Login: 0x${packetId.toString(16)}")
                packetData.readByteArray() // ignore
            }
        }
        return true
    }

    /** What a real client of this version would refuse, checked on the way past. */
    private val checks = Checks(profile.protocol) { println("Bot $id: $it") }

    private suspend fun handleConfigurationPacket(
        packetId: Int,
        packetData: Source,
        scope: kotlinx.coroutines.CoroutineScope,
    ): Boolean {
        when (packetId) {
            profile.configuration.clientboundDisconnect -> {
                val reason = packetData.readByteArray().decodeToString()
                println("Bot $id was disconnected while configuring. Reason: $reason")
                return false
            }
            profile.configuration.clientboundKeepAlive -> {
                val keepAliveId = packetData.readByteArray()
                sendChannel.writePacket(profile.configuration.serverboundKeepAlive, compressionThreshold) {
                    write(keepAliveId)
                }
            }
            profile.configuration.clientboundFinish -> {
                packetData.readByteArray()
                sendChannel.writePacket(profile.configuration.serverboundFinish, compressionThreshold) {}
                state = ProtocolState.PLAY
                println("Bot $id joined the game (Play State)")
                if (physicsEnabled) {
                    scope.launchPhysicsLoop()
                }
            }
            profile.configuration.clientboundSelectKnownPacks.takeIf { it >= 0 } -> {
                packetData.readByteArray()
                sendChannel.writePacket(profile.configuration.serverboundSelectKnownPacks, compressionThreshold) {
                    writeVarInt(0)
                }
            }
            checks.packetId("minecraft:registry_data").takeIf { it >= 0 } -> checks.registryData(packetData)
            checks.packetId("minecraft:update_tags").takeIf { it >= 0 } -> checks.tags(packetData)
            else -> {
                packetData.readByteArray()
            }
        }
        return true
    }

    private fun kotlinx.coroutines.CoroutineScope.launchPhysicsLoop() {
        launch {
            println("Bot $id starting physics loop")
            while (state == ProtocolState.PLAY) {
                if (positionReceived) {
                    // Update wandering state
                    if (wanderTicks <= 0) {
                        if (Random.nextDouble() < 0.2) { // 20% chance to start wandering
                            isWandering = true
                            wanderTicks = Random.nextInt(20, 100) // Wander for 1 to 5 seconds
                            yaw = Random.nextFloat() * 360f - 180f
                        } else {
                            isWandering = false
                            wanderTicks = Random.nextInt(20, 60) // Idle for 1 to 3 seconds
                        }
                    } else {
                        wanderTicks--
                    }

                    // Random jumping
                    if (onGround && isWandering && Random.nextDouble() < 0.05) {
                        yVelocity = 0.42f.toDouble()
                        onGround = false
                    }

                    // Minecraft gravity
                    if (!onGround) {
                        yVelocity = (yVelocity - 0.08) * 0.98
                        if (yVelocity < -3.92) {
                            yVelocity = -3.92
                        }
                    }

                    // Movement
                    if (isWandering && onGround) {
                        val speed = 0.15
                        val yawRad = yaw.toDouble() * kotlin.math.PI / 180.0
                        val dx = -sin(yawRad) * speed
                        val dz = cos(yawRad) * speed
                        x += dx
                        z += dz
                    } else if (isWandering && !onGround) {
                        // Slower movement in air
                        val speed = 0.02
                        val yawRad = yaw.toDouble() * kotlin.math.PI / 180.0
                        val dx = -sin(yawRad) * speed
                        val dz = cos(yawRad) * speed
                        x += dx
                        z += dz
                    }

                    y += yVelocity

                    // Very simple floor collision to prevent falling into the void forever
                    if (y <= 63.0) {
                        y = 63.0
                        yVelocity = 0.0
                        onGround = true
                    } else {
                        onGround = false
                    }

                    sendPositionLook()
                }
                delay(50) // 20 TPS
            }
        }
    }

    private suspend fun handlePlayPacket(packetId: Int, packetData: Source): Boolean {
        when (packetId) {
            profile.play.clientboundDisconnect -> {
                val reason = packetData.readByteArray().decodeToString()
                println("Bot $id was kicked. Reason: $reason")
                return false
            }
            profile.play.clientboundKeepAlive -> {
                val keepAliveId = packetData.readByteArray()
                sendChannel.writePacket(profile.play.serverboundKeepAlive, compressionThreshold) {
                    write(keepAliveId)
                }
            }
            profile.play.clientboundPlayerPosition -> {
                // Before 1.21.2 the id is at the end, after the angles and the relative flags.
                val teleportId: Int
                val parsedX: Double
                val parsedY: Double
                val parsedZ: Double
                if (profile.positionStartsWithTeleportId) {
                    teleportId = packetData.readVarInt()
                    parsedX = packetData.readDouble()
                    parsedY = packetData.readDouble()
                    parsedZ = packetData.readDouble()
                } else {
                    parsedX = packetData.readDouble()
                    parsedY = packetData.readDouble()
                    parsedZ = packetData.readDouble()
                    packetData.readFloat()
                    packetData.readFloat()
                    packetData.readByte()
                    teleportId = if (profile.positionIsAcknowledged) packetData.readVarInt() else 0
                }
                
                // Set our internal position based on server authority
                x = parsedX
                y = parsedY
                z = parsedZ
                yVelocity = 0.0
                positionReceived = true
                
                packetData.readByteArray() // discard remaining bytes (dx, dy, dz, yaw, pitch, flags)
                
                if (profile.positionIsAcknowledged) {
                    sendChannel.writePacket(profile.play.serverboundAcceptTeleportation, compressionThreshold) {
                        writeVarInt(teleportId)
                    }
                }

                // Every sync puts the bot back on the spawn, so the target is claimed again after
                // each one or the swarm quietly collapses into a single spot.
                if (target != null) {
                    x = target.x
                    z = target.z
                    sendPositionLook()
                }
            }
            checks.packetId("minecraft:commands").takeIf { it >= 0 } -> checks.commands(packetData)
            checks.packetId("minecraft:update_recipes").takeIf { it >= 0 } -> checks.updateRecipes(packetData)
            else -> {
                packetData.readByteArray()
            }
        }
        return true
    }

    private suspend fun sendPositionLook() {
        sendChannel.writePacket(profile.play.serverboundMovePosRot, compressionThreshold) {
            writeDouble(x)
            writeDouble(y)
            writeDouble(z)
            writeFloat(yaw)
            writeFloat(pitch)
            writeByte(if (onGround) 1 else 0)
        }
    }

    private suspend fun sendClientInformation() {
        val id =
            if (profile.hasConfigurationPhase) {
                profile.configuration.serverboundClientInformation
            } else {
                profile.play.serverboundClientInformation
            }
        sendChannel.writePacket(id, compressionThreshold) {
            writeString("en_us")
            writeByte(2) // view distance
            writeVarInt(0) // chat mode
            writeByte(1) // chat colors
            writeByte(0x7F) // skin parts
            if (profile.settingsHaveMainHand) writeVarInt(1)
            if (profile.settingsHaveTextFiltering) writeByte(0)
            if (profile.settingsHaveServerListing) writeByte(1)
            if (profile.settingsHaveParticleStatus) {
                writeVarInt(1) // particle status
            }
        }
    }
}
