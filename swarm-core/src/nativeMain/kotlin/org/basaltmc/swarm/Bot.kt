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

enum class ProtocolState {
    LOGIN,
    CONFIGURATION,
    PLAY
}

class Bot(private val id: Int, private val host: String, private val port: Int, private val physicsEnabled: Boolean = false) {
    private var state = ProtocolState.LOGIN
    private var compressionThreshold = -1
    private lateinit var sendChannel: ByteWriteChannel

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
                // TODO: Simulate gravity and movement, send Player Position packet
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
                // The rest of the payload is doubles/floats for position, we can ignore them for now
                // just acknowledge the teleport so the server thinks we're fully loaded
                packetData.readByteArray()
                
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
