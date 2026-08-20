package org.basaltmc.swarm

import io.ktor.utils.io.ByteReadChannel
import io.ktor.utils.io.ByteWriteChannel
import io.ktor.utils.io.readByte
import io.ktor.utils.io.writeFully
import io.ktor.utils.io.core.buildPacket
import io.ktor.utils.io.core.writeFully
import kotlinx.io.readByteArray
import io.ktor.utils.io.core.BytePacketBuilder

suspend fun ByteReadChannel.readVarInt(): Int {
    var numRead = 0
    var result = 0
    var read: Byte
    do {
        read = readByte()
        val value = (read.toInt() and 0b01111111)
        result = result or (value shl (7 * numRead))
        numRead++
        if (numRead > 5) {
            throw RuntimeException("VarInt is too big")
        }
    } while ((read.toInt() and 0b10000000) != 0)
    return result
}

fun BytePacketBuilder.writeVarInt(value: Int) {
    var v = value
    do {
        var temp = (v and 0b01111111).toByte()
        v = v ushr 7
        if (v != 0) {
            temp = (temp.toInt() or 0b10000000).toByte()
        }
        writeByte(temp)
    } while (v != 0)
}

fun BytePacketBuilder.writeString(value: String) {
    val bytes = value.encodeToByteArray()
    writeVarInt(bytes.size)
    writeFully(bytes)
}

suspend fun ByteWriteChannel.writePacket(packetId: Int, builderAction: BytePacketBuilder.() -> Unit) {
    val payloadPacket = buildPacket {
        writeVarInt(packetId)
        builderAction()
    }
    val payloadBytes = payloadPacket.readByteArray()
    
    val lengthPacket = buildPacket {
        writeVarInt(payloadBytes.size)
    }
    val lengthBytes = lengthPacket.readByteArray()
    
    writeFully(lengthBytes)
    writeFully(payloadBytes)
    flush()
}
