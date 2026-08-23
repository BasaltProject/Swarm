package org.basaltmc.swarm

import kotlinx.io.Buffer
import kotlinx.io.writeString
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

/**
 * The checks against the four things that actually took a real client down.
 *
 * Each of these is a packet a server really sent and a client really refused, written here so that
 * the next one is found by a bot and not by somebody reading a disconnect report.
 */
class ChecksTest {
    @Test
    fun `a tag group naming a registry the version does not have is caught`() {
        val complaints = mutableListOf<String>()
        val checks = Checks(PROTOCOL_1_21_10) { complaints += it }
        val body =
            buffer {
                writeVarInt(1)
                writeVarString("minecraft:timeline")
                writeVarInt(0)
            }
        checks.tags(body)
        assertEquals(1, checks.problems)
        assertTrue(complaints.single().contains("minecraft:timeline"), complaints.toString())
    }

    @Test
    fun `a command argument naming one is caught too`() {
        val complaints = mutableListOf<String>()
        val checks = Checks(PROTOCOL_1_21_11) { complaints += it }
        val body =
            buffer {
                writeVarInt(2)
                // The root, with one child.
                writeByte(0)
                writeVarInt(1)
                writeVarInt(1)
                // An argument node that names a registry 1.21.11 does not have.
                writeByte(2)
                writeVarInt(0)
                writeVarString("clock")
                writeVarInt(RESOURCE_PARSER)
                writeVarString("minecraft:world_clock")
                writeVarInt(0)
            }
        checks.commands(body)
        assertEquals(1, checks.problems)
        assertTrue(complaints.single().contains("minecraft:world_clock"), complaints.toString())
    }

    @Test
    fun `an item past the end of that version's list is caught`() {
        val complaints = mutableListOf<String>()
        val checks = Checks(PROTOCOL_26_1) { complaints += it }
        val body =
            buffer {
                writeVarInt(0)
                writeVarInt(1)
                writeVarInt(2)
                writeVarInt(6)
                writeVarInt(5)
                writeVarInt(ITEM_PAST_26_1)
                writeVarInt(1)
                writeVarInt(0)
                writeVarInt(0)
            }
        checks.updateRecipes(body)
        assertEquals(1, checks.problems)
        assertTrue(complaints.single().contains("$ITEM_PAST_26_1"), complaints.toString())
    }

    @Test
    fun `and a registry that version never had is caught when it is sent one`() {
        val complaints = mutableListOf<String>()
        val checks = Checks(PROTOCOL_1_21_10) { complaints += it }
        checks.registryData(
            buffer {
                writeVarString("minecraft:world_clock")
                writeVarInt(0)
            },
        )
        assertEquals(1, checks.problems)
    }

    @Test
    fun `what a version does have raises nothing`() {
        val checks = Checks(PROTOCOL_1_21_10) { }
        checks.tags(
            buffer {
                writeVarInt(1)
                writeVarString("minecraft:block")
                writeVarInt(1)
                writeVarString("minecraft:logs")
                writeVarInt(1)
                writeVarInt(1)
            },
        )
        assertEquals(0, checks.problems)
    }

    private fun buffer(write: Buffer.() -> Unit): Buffer = Buffer().apply(write)

    private fun Buffer.writeVarInt(value: Int) {
        var remaining = value
        while (true) {
            if (remaining and 0x7F.inv() == 0) {
                writeByte(remaining.toByte())
                return
            }
            writeByte(((remaining and 0x7F) or 0x80).toByte())
            remaining = remaining ushr 7
        }
    }

    private fun Buffer.writeVarString(value: String) {
        writeVarInt(value.length)
        writeString(value)
    }

    private companion object {
        const val PROTOCOL_1_21_10 = 773
        const val PROTOCOL_1_21_11 = 774
        const val PROTOCOL_26_1 = 775

        /** `minecraft:resource`, the parser that names a registry. */
        const val RESOURCE_PARSER = 46

        /** One past the last item 26.1 has, which is what took a real client down. */
        const val ITEM_PAST_26_1 = 1507
    }
}
