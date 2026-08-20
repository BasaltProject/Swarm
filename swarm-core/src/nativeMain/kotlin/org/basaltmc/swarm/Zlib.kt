@file:OptIn(ExperimentalForeignApi::class, ExperimentalUnsignedTypes::class)

package org.basaltmc.swarm

import kotlinx.cinterop.ExperimentalForeignApi
import kotlinx.cinterop.addressOf
import kotlinx.cinterop.alloc
import kotlinx.cinterop.convert
import kotlinx.cinterop.memScoped
import kotlinx.cinterop.ptr
import kotlinx.cinterop.usePinned
import kotlinx.cinterop.value
import platform.zlib.Z_BUF_ERROR
import platform.zlib.Z_OK
import platform.zlib.compress2
import platform.zlib.compressBound
import platform.zlib.uLongVar
import platform.zlib.uncompress

public class ZlibBufferTooSmallException : Exception("zlib destination buffer too small")

public class ZlibException(
    public val code: Int,
) : Exception("zlib failed with code $code")

public object Zlib {
    public fun compress(
        data: ByteArray,
        level: Int = 6,
    ): ByteArray {
        if (data.isEmpty()) return ByteArray(0)
        val bound = compressBound(data.size.convert())
        val dest = UByteArray(bound.toInt())
        val source = data.asUByteArray()
        return memScoped {
            val destLen = alloc<uLongVar>()
            destLen.value = bound
            val result =
                source.usePinned { srcPinned ->
                    dest.usePinned { destPinned ->
                        compress2(
                            destPinned.addressOf(0),
                            destLen.ptr,
                            srcPinned.addressOf(0),
                            data.size.convert(),
                            level,
                        )
                    }
                }
            if (result != Z_OK) throw ZlibException(result)
            dest.asByteArray().copyOf(destLen.value.toInt())
        }
    }

    public fun decompress(
        data: ByteArray,
        uncompressedSize: Int,
    ): ByteArray {
        if (uncompressedSize == 0) return ByteArray(0)
        require(data.isNotEmpty()) { "Cannot decompress empty input into $uncompressedSize bytes" }
        val dest = UByteArray(uncompressedSize)
        val source = data.asUByteArray()
        return memScoped {
            val destLen = alloc<uLongVar>()
            destLen.value = uncompressedSize.convert()
            val result =
                source.usePinned { srcPinned ->
                    dest.usePinned { destPinned ->
                        uncompress(destPinned.addressOf(0), destLen.ptr, srcPinned.addressOf(0), data.size.convert())
                    }
                }
            when (result) {
                Z_OK -> {
                    val written = destLen.value.toInt()
                    if (written == uncompressedSize) dest.asByteArray() else dest.asByteArray().copyOf(written)
                }
                Z_BUF_ERROR -> throw ZlibBufferTooSmallException()
                else -> throw ZlibException(result)
            }
        }
    }
}
