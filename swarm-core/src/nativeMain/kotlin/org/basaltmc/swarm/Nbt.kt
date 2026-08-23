package org.basaltmc.swarm

import kotlinx.io.Source
import kotlinx.io.readByteArray

/**
 * Enough NBT to walk past a tag without understanding it.
 *
 * A registry element is a tag, and a bot has to get past one to read the name of the next entry.
 * Nothing here builds a tree: it is a reader that skips, which is all a check of names needs.
 */
object Nbt {
    /** Skips one tag, id and all, as it travels in a packet: no name on the outermost one. */
    fun skip(source: Source) {
        val type = source.readByte().toInt()
        if (type != END) skipPayload(source, type)
    }

    private fun skipPayload(
        source: Source,
        type: Int,
    ) {
        when (type) {
            BYTE -> source.readByteArray(1)
            SHORT -> source.readByteArray(2)
            INT, FLOAT -> source.readByteArray(4)
            LONG, DOUBLE -> source.readByteArray(8)
            BYTE_ARRAY -> source.readByteArray(readInt(source))
            STRING -> source.readByteArray(readUnsignedShort(source))
            LIST -> {
                val elements = source.readByte().toInt()
                val count = readInt(source)
                repeat(count) { if (elements != END) skipPayload(source, elements) }
            }
            COMPOUND -> {
                while (true) {
                    val entry = source.readByte().toInt()
                    if (entry == END) return
                    source.readByteArray(readUnsignedShort(source))
                    skipPayload(source, entry)
                }
            }
            INT_ARRAY -> source.readByteArray(readInt(source) * 4)
            LONG_ARRAY -> source.readByteArray(readInt(source) * 8)
            else -> throw IllegalStateException("unknown NBT tag $type")
        }
    }

    private fun readInt(source: Source): Int {
        val bytes = source.readByteArray(4)
        return (bytes[0].toInt() and 0xFF shl 24) or (bytes[1].toInt() and 0xFF shl 16) or
            (bytes[2].toInt() and 0xFF shl 8) or (bytes[3].toInt() and 0xFF)
    }

    private fun readUnsignedShort(source: Source): Int {
        val bytes = source.readByteArray(2)
        return (bytes[0].toInt() and 0xFF shl 8) or (bytes[1].toInt() and 0xFF)
    }

    private const val END = 0
    private const val BYTE = 1
    private const val SHORT = 2
    private const val INT = 3
    private const val LONG = 4
    private const val FLOAT = 5
    private const val DOUBLE = 6
    private const val BYTE_ARRAY = 7
    private const val STRING = 8
    private const val LIST = 9
    private const val COMPOUND = 10
    private const val INT_ARRAY = 11
    private const val LONG_ARRAY = 12
}
