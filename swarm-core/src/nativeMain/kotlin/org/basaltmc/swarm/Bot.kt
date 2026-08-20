package org.basaltmc.swarm

import io.ktor.network.selector.SelectorManager
import io.ktor.network.sockets.aSocket
import io.ktor.network.sockets.openReadChannel
import io.ktor.network.sockets.openWriteChannel
import io.ktor.utils.io.ByteReadChannel
import io.ktor.utils.io.ByteWriteChannel
import io.ktor.utils.io.core.writeFully
import kotlinx.io.readByteArray
import io.ktor.utils.io.readPacket
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

class Bot(private val id: Int, private val host: String, private val port: Int) {
    suspend fun connect() {
        val selectorManager = SelectorManager(Dispatchers.Default)
        try {
            val socket = aSocket(selectorManager).tcp().connect(host, port)
            val receiveChannel = socket.openReadChannel()
            val sendChannel = socket.openWriteChannel(autoFlush = true)

            println("Bot $id connected to $host:$port")

            // Handshake (Packet ID 0x00)
            sendChannel.writePacket(0x00, -1) {
                writeVarInt(776) // Protocol version for 26.2
                writeString(host)
                writeShort(port.toShort())
                writeVarInt(2) // Next state: Login
            }

            // Login Start (Packet ID 0x00 in Login state)
            sendChannel.writePacket(0x00, -1) {
                writeString("Bot_$id")
                val uuidBytes = kotlin.random.Random.nextBytes(16)
                uuidBytes[6] = (uuidBytes[6].toInt() and 0x0f or 0x40).toByte()
                uuidBytes[8] = (uuidBytes[8].toInt() and 0x3f or 0x80).toByte()
                writeFully(uuidBytes)
            }

            println("Bot $id sent Login Start")
            
            var state = 1 // 1=LOGIN, 2=CONFIGURATION, 3=PLAY
            var compressionThreshold = -1

            // Keep alive and state machine loop
            while (true) {
                val length = receiveChannel.readVarInt()
                val rawPacketData = receiveChannel.readPacket(length)
                
                val packetData: kotlinx.io.Source
                if (compressionThreshold >= 0) {
                    val dataLength = rawPacketData.readVarInt()
                    if (dataLength == 0) {
                        packetData = rawPacketData
                    } else {
                        val compressedData = rawPacketData.readByteArray()
                        val decompressed = Zlib.decompress(compressedData, dataLength)
                        packetData = kotlinx.io.Buffer().apply { write(decompressed) }
                    }
                } else {
                    packetData = rawPacketData
                }

                val packetId = packetData.readVarInt()

                when (state) {
                    1 -> { // LOGIN
                        if (packetId == 0x03) { // Login Compression
                            compressionThreshold = packetData.readVarInt()
                            println("Bot $id enabled compression. Threshold: $compressionThreshold")
                        } else if (packetId == 0x02) { // Login Success
                            packetData.readByteArray()
                            println("Bot $id received Login Success. Sending Acknowledged.")
                            sendChannel.writePacket(0x03, compressionThreshold) {}
                            state = 2
                            
                            sendChannel.writePacket(0x00, compressionThreshold) {
                                writeString("en_us")
                                writeByte(2)
                                writeVarInt(0)
                                writeByte(1)
                                writeByte(0x7F)
                                writeVarInt(1)
                                writeByte(0)
                                writeByte(1)
                                writeVarInt(1) // particleStatus
                            }
                        } else if (packetId == 0x00) { // Disconnect
                            val reason = packetData.readByteArray().decodeToString()
                            println("Bot $id disconnected in Login. Reason: $reason")
                            return
                        } else {
                            println("Bot $id received unknown packet in Login: $packetId")
                            packetData.readByteArray()
                        }
                    }
                    2 -> { // CONFIGURATION
                        if (packetId == 0x03) { // Finish Configuration
                            packetData.readByteArray()
                            sendChannel.writePacket(0x03, compressionThreshold) {}
                            state = 3
                            println("Bot $id joined the game (Play State)")
                        } else if (packetId == 0x02) { // Keep Alive in Config
                            val keepAliveId = packetData.readByteArray()
                            sendChannel.writePacket(0x02, compressionThreshold) {
                                writeFully(keepAliveId)
                            }
                        } else if (packetId == 0x00) { // Clientbound Plugin Message
                            packetData.readByteArray()
                        } else if (packetId == 0x0e) { // Select Known Packs
                            packetData.readByteArray()
                            // Respond with 0 known packs
                            sendChannel.writePacket(0x07, compressionThreshold) {
                                writeVarInt(0)
                            }
                        } else {
                            val unknownData = packetData.readByteArray()
                            println("Bot $id received unknown packet in config: $packetId")
                        }
                    }
                    3 -> { // PLAY
                        // Keep alive ID in Play state
                        packetData.readByteArray()
                    }
                }
            }
            
        } catch (e: Exception) {
            println("Bot $id failed to connect: ${e.message}")
        }
    }
}
