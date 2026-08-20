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

class Bot(private val id: Int, private val host: String, private val port: Int, private val physicsEnabled: Boolean = false) {
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
            writeVarInt(776) // Protocol version for 26.2
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

    private suspend fun handlePacket(packetId: Int, packetData: Source, scope: kotlinx.coroutines.CoroutineScope): Boolean {
        return when (state) {
            ProtocolState.LOGIN -> handleLoginPacket(packetId, packetData)
            ProtocolState.CONFIGURATION -> handleConfigurationPacket(packetId, packetData, scope)
            ProtocolState.PLAY -> handlePlayPacket(packetId, packetData)
        }
    }

    private suspend fun handleLoginPacket(packetId: Int, packetData: Source): Boolean {
        when (packetId) {
            0x00 -> { // Disconnect
                val reason = packetData.readByteArray().decodeToString()
                println("Bot $id disconnected in Login. Reason: $reason")
                return false
            }
            0x02 -> { // Login Success
                packetData.readByteArray() // discard payload
                println("Bot $id received Login Success. Sending Acknowledged.")
                sendChannel.writePacket(0x03, compressionThreshold) {}
                state = ProtocolState.CONFIGURATION
                sendClientInformation()
            }
            0x03 -> { // Login Compression
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

    private suspend fun handleConfigurationPacket(packetId: Int, packetData: Source, scope: kotlinx.coroutines.CoroutineScope): Boolean {
        when (packetId) {
            0x00 -> { // Clientbound Plugin Message
                packetData.readByteArray()
            }
            0x01 -> { // Brand
                packetData.readByteArray()
            }
            0x02 -> { // Keep Alive
                val keepAliveId = packetData.readByteArray()
                sendChannel.writePacket(0x02, compressionThreshold) {
                    write(keepAliveId)
                }
            }
            0x03 -> { // Finish Configuration
                packetData.readByteArray()
                sendChannel.writePacket(0x03, compressionThreshold) {}
                state = ProtocolState.PLAY
                println("Bot $id joined the game (Play State)")
                if (physicsEnabled) {
                    scope.launchPhysicsLoop()
                }
            }
            0x0e -> { // Select Known Packs
                packetData.readByteArray()
                sendChannel.writePacket(0x07, compressionThreshold) {
                    writeVarInt(0)
                }
            }
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

                    // Send ServerboundPositionLookPacket (0x1f)
                    sendChannel.writePacket(0x1f, compressionThreshold) {
                        writeDouble(x)
                        writeDouble(y)
                        writeDouble(z)
                        writeFloat(yaw)
                        writeFloat(pitch)
                        writeByte(if (onGround) 1 else 0)
                    }
                }
                delay(50) // 20 TPS
            }
        }
    }

    private suspend fun handlePlayPacket(packetId: Int, packetData: Source): Boolean {
        when (packetId) {
            0x2c -> { // Keep Alive
                val keepAliveId = packetData.readByteArray()
                sendChannel.writePacket(0x1c, compressionThreshold) {
                    write(keepAliveId)
                }
            }
            0x48 -> { // Player Position (Clientbound)
                val teleportId = packetData.readVarInt()
                val parsedX = packetData.readDouble()
                val parsedY = packetData.readDouble()
                val parsedZ = packetData.readDouble()
                
                // Set our internal position based on server authority
                x = parsedX
                y = parsedY
                z = parsedZ
                yVelocity = 0.0
                positionReceived = true
                
                packetData.readByteArray() // discard remaining bytes (dx, dy, dz, yaw, pitch, flags)
                
                sendChannel.writePacket(0x00, compressionThreshold) { // Accept Teleportation
                    writeVarInt(teleportId)
                }
            }
            else -> {
                packetData.readByteArray()
            }
        }
        return true
    }

    private suspend fun sendClientInformation() {
        sendChannel.writePacket(0x00, compressionThreshold) {
            writeString("en_us")
            writeByte(2) // view distance
            writeVarInt(0) // chat mode
            writeByte(1) // chat colors
            writeByte(0x7F) // skin parts
            writeVarInt(1) // main hand
            writeByte(0) // text filtering
            writeByte(1) // server listings
            writeVarInt(1) // particle status
        }
    }
}
