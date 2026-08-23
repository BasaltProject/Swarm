package org.basaltmc.swarm

import kotlinx.io.Source
import kotlinx.io.readByteArray
import kotlinx.io.readString

/**
 * What a real client would refuse, checked on the packets a bot can read.
 *
 * What breaks a client is not the framing: it is a registry it does not have or an id past the end
 * of one, and the numbers to check both come from that version's own jar (see [KnownVersions]).
 *
 * Nothing throws: a bot reports and carries on, so one connection gives the whole list.
 */
class Checks(
    private val protocol: Int,
    private val report: (String) -> Unit,
) {
    private val complained = mutableSetOf<String>()

    var problems: Int = 0
        private set

    val checkable: Boolean get() = KnownVersions.isChecked(protocol)

    fun packetId(packet: String): Int = KnownVersions.packetId(protocol, packet)

    /**
     * One registry's worth of elements.
     *
     * The name of the registry and the name of every entry: an entry the version has never heard
     * of is one it will try to parse against a shape it does not have, and it stops loading its
     * registries altogether rather than skipping it.
     */
    fun registryData(data: Source) {
        val registry = data.readVarString()
        if (!KnownVersions.knows(protocol, registry)) {
            complain("registry $registry does not exist in this version, and being sent it is fatal")
        }
        val entries = data.readVarInt()
        repeat(entries) {
            val entry = data.readVarString()
            if (!KnownVersions.hasEntry(protocol, registry, entry)) {
                complain("$registry has no $entry in this version, and its element will not parse")
            }
            if (data.readByte().toInt() != 0) Nbt.skip(data)
        }
    }

    /** The tags: every group names a registry, and every id in it has to be one of that registry's. */
    fun tags(data: Source) {
        val groups = data.readVarInt()
        repeat(groups) {
            val registry = data.readVarString()
            if (!KnownVersions.knows(protocol, registry)) {
                complain("tags name the registry $registry, which does not exist here")
            }
            val size = KnownVersions.size(protocol, registry)
            val tags = data.readVarInt()
            repeat(tags) {
                data.readVarString()
                val ids = data.readVarInt()
                repeat(ids) {
                    val id = data.readVarInt()
                    if (size >= 0 && id >= size) {
                        complain("a tag of $registry points at $id, and that registry ends at ${size - 1}")
                    }
                }
            }
        }
    }

    /**
     * The command tree, whose arguments name registries the client looks up as it reads.
     *
     * Only the shape is walked: a node's parser properties are skipped by knowing which parsers
     * have any, which is the same handful every version has had since 1.13.
     */
    fun commands(data: Source) {
        val nodes = data.readVarInt()
        repeat(nodes) {
            val flags = data.readByte().toInt()
            repeat(data.readVarInt()) { data.readVarInt() }
            if (flags and REDIRECT != 0) data.readVarInt()
            val kind = flags and KIND
            if (kind != ROOT) data.readVarString()
            if (kind == ARGUMENT) {
                val parser = data.readVarInt()
                val registry = readParserProperties(data, parser)
                if (registry != null && !KnownVersions.knows(protocol, registry)) {
                    complain("a command argument names the registry $registry, which does not exist here")
                }
                if (flags and CUSTOM_SUGGESTIONS != 0) data.readVarString()
            }
        }
    }

    /** The stonecutter's list: an item and the kind of slot it is drawn as, both registry ids. */
    fun updateRecipes(data: Source) {
        repeat(data.readVarInt()) {
            data.readVarString()
            repeat(data.readVarInt()) { data.readVarInt() }
        }
        val items = KnownVersions.size(protocol, "minecraft:item")
        val displays = KnownVersions.size(protocol, "minecraft:slot_display")
        repeat(data.readVarInt()) {
            repeat(data.readVarInt() - 1) { checkId(data.readVarInt(), items, "minecraft:item") }
            checkId(data.readVarInt(), displays, "minecraft:slot_display")
            checkId(data.readVarInt(), items, "minecraft:item")
            data.readVarInt()
            data.readVarInt()
            data.readVarInt()
        }
    }

    private fun checkId(
        id: Int,
        size: Int,
        registry: String,
    ) {
        if (size >= 0 && id >= size) {
            complain("$registry has ${size} entries here and the server sent $id")
        }
    }

    /**
     * Walks past a parser's own configuration, answering the registry it names when it names one.
     *
     * parity: `ArgumentTypeInfos`, whose parsers with properties are the numeric ones, the strings,
     * the entity selector, the score holder, the time and the resource kinds.
     */
    private fun readParserProperties(
        data: Source,
        parser: Int,
    ): String? =
        when (parser) {
            BRIGADIER_FLOAT, BRIGADIER_DOUBLE -> {
                val flags = data.readByte().toInt()
                if (flags and 1 != 0) data.readByteArray(if (parser == BRIGADIER_DOUBLE) 8 else 4)
                if (flags and 2 != 0) data.readByteArray(if (parser == BRIGADIER_DOUBLE) 8 else 4)
                null
            }
            BRIGADIER_INTEGER, BRIGADIER_LONG -> {
                val flags = data.readByte().toInt()
                if (flags and 1 != 0) data.readByteArray(if (parser == BRIGADIER_LONG) 8 else 4)
                if (flags and 2 != 0) data.readByteArray(if (parser == BRIGADIER_LONG) 8 else 4)
                null
            }
            BRIGADIER_STRING, ENTITY, SCORE_HOLDER -> {
                data.readByte()
                null
            }
            TIME -> {
                data.readByteArray(4)
                null
            }
            RESOURCE, RESOURCE_KEY, RESOURCE_OR_TAG, RESOURCE_OR_TAG_KEY, RESOURCE_SELECTOR ->
                data.readVarString()
            else -> null
        }

    private fun complain(what: String) {
        if (!complained.add(what)) return
        problems++
        report(what)
    }

    private fun Source.readVarString(): String {
        val length = readVarInt()
        return readString(length.toLong())
    }

    private companion object {
        const val KIND = 3
        const val ROOT = 0
        const val ARGUMENT = 2
        const val REDIRECT = 8
        const val CUSTOM_SUGGESTIONS = 16

        /** parity: `ArgumentTypeInfos`' own registration order, which has not moved since 1.19. */
        const val BRIGADIER_DOUBLE = 1
        const val BRIGADIER_FLOAT = 2
        const val BRIGADIER_INTEGER = 3
        const val BRIGADIER_LONG = 4
        const val BRIGADIER_STRING = 5
        const val ENTITY = 6
        const val SCORE_HOLDER = 29
        const val TIME = 41
        const val RESOURCE_OR_TAG = 44
        const val RESOURCE_OR_TAG_KEY = 45
        const val RESOURCE = 46
        const val RESOURCE_KEY = 47
        const val RESOURCE_SELECTOR = 48
    }
}
