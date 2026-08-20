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
            sendChannel.writePacket(0x00) {
                writeVarInt(776) // Protocol version for 26.2
                writeString(host)
                writeShort(port.toShort())
                writeVarInt(2) // Next state: Login
            }

            // Login Start (Packet ID 0x00 in Login state)
            sendChannel.writePacket(0x00) {
                writeString("Bot_$id")
                // UUID (16 bytes)
                val uuidBytes = kotlin.random.Random.nextBytes(16)
                // Set UUID version to 4 (random) and variant to 2 (Leach-Salz)
                uuidBytes[6] = (uuidBytes[6].toInt() and 0x0f or 0x40).toByte()
                uuidBytes[8] = (uuidBytes[8].toInt() and 0x3f or 0x80).toByte()
                writeFully(uuidBytes)
            }

            println("Bot $id sent Login Start")
            
            // Keep alive loop
            while (true) {
                val length = receiveChannel.readVarInt()
                val packetData = receiveChannel.readPacket(length)
                // Parse packet ID (VarInt) manually from the packet
                var numRead = 0
                var packetId = 0
                var read: Byte
                do {
                    read = packetData.readByte()
                    val value = (read.toInt() and 0b01111111)
                    packetId = packetId or (value shl (7 * numRead))
                    numRead++
                } while ((read.toInt() and 0b10000000) != 0)

                // If it's a Keep Alive packet (Clientbound Play 0x24 in 1.20, might differ in 26.2, we need to respond)
                // For a stress tester, we might need to properly parse Login Success -> Play state transition
                // For now just discard unhandled packets
                packetData.readByteArray()
            }
            
        } catch (e: Exception) {
            println("Bot $id failed to connect: ${e.message}")
        }
    }
}
