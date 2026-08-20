package org.basaltmc.swarm

import io.ktor.utils.io.ByteReadChannel
import io.ktor.utils.io.ByteWriteChannel
import io.ktor.utils.io.readByte
import io.ktor.utils.io.writeFully
import kotlinx.io.Buffer
import kotlinx.io.Source
import kotlinx.io.readByteArray

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

fun Source.readVarInt(): Int {
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

fun Buffer.writeVarInt(value: Int) {
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

fun Buffer.writeDouble(value: Double) {
    writeLong(value.toRawBits())
}

fun Source.readDouble(): Double {
    return Double.fromBits(readLong())
}

fun Buffer.writeString(value: String) {
    val bytes = value.encodeToByteArray()
    writeVarInt(bytes.size)
    write(bytes)
}

suspend fun ByteWriteChannel.writePacket(packetId: Int, compressionThreshold: Int, builderAction: Buffer.() -> Unit) {
    val payloadBuffer = Buffer().apply {
        writeVarInt(packetId)
        builderAction()
    }
    val payloadBytes = payloadBuffer.readByteArray()
    
    val finalData: ByteArray
    if (compressionThreshold >= 0) {
        if (payloadBytes.size >= compressionThreshold) {
            val compressed = Zlib.compress(payloadBytes)
            val dataLengthBuffer = Buffer().apply { writeVarInt(payloadBytes.size) }
            finalData = dataLengthBuffer.readByteArray() + compressed
        } else {
            val dataLengthBuffer = Buffer().apply { writeVarInt(0) }
            finalData = dataLengthBuffer.readByteArray() + payloadBytes
        }
    } else {
        finalData = payloadBytes
    }
    
    val lengthBuffer = Buffer().apply {
        writeVarInt(finalData.size)
    }
    
    writeFully(lengthBuffer.readByteArray())
    writeFully(finalData)
    flush()
}
