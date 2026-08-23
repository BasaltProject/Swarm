package org.basaltmc.swarm

/**
 * One version a bot can pretend to be: the number it sends in the handshake, and the packet ids
 * that number implies.
 *
 * The ids are here rather than spread through the bot because they are the only thing that changes
 * between versions. Adding a version is adding an entry with its own numbers, taken from the
 * official jar's own packet report, and nothing else in the tool moves.
 */
data class ProtocolProfile(
    val protocol: Int,
    val label: String,
    val login: LoginIds = LoginIds(),
    val configuration: ConfigurationIds = ConfigurationIds(),
    val play: PlayIds = PlayIds(),
    /**
     * Whether the version has a configuration phase at all.
     *
     * It arrived in 1.20.2. Before that a client goes from logging in straight to playing, and
     * there is nothing to acknowledge on the way.
     */
    val hasConfigurationPhase: Boolean = true,
    /** 1.21.2 added the particle setting to what a client tells the server about itself. */
    val settingsHaveParticleStatus: Boolean = true,
    /**
     * Whether a position has to be acknowledged at all.
     *
     * The teleport id and the packet that confirms it are 1.9's. A 1.8 client is simply moved and
     * says nothing back.
     */
    val positionIsAcknowledged: Boolean = true,
    /** 1.9 asked which hand is the main one; 1.8 has one hand and does not say. */
    val settingsHaveMainHand: Boolean = true,
    /** 1.17 added whether the client filters text, and 1.18 whether it wants to be listed. */
    val settingsHaveTextFiltering: Boolean = true,
    val settingsHaveServerListing: Boolean = true,
    /**
     * Whether the teleport id comes before the position rather than after it.
     *
     * 1.21.2 moved it to the front and put a delta movement beside the position; before that the
     * packet ends with the id.
     */
    val positionStartsWithTeleportId: Boolean = true,
) {
    data class LoginIds(
        val clientboundDisconnect: Int = 0,
        val clientboundCompression: Int = 3,
        val clientboundFinished: Int = 2,
        val serverboundHello: Int = 0,
        val serverboundAcknowledged: Int = 3,
    )

    data class ConfigurationIds(
        val clientboundDisconnect: Int = 2,
        val clientboundKeepAlive: Int = 4,
        val clientboundFinish: Int = 3,
        /** -1 where the version has no such packet, which is everything before 1.20.5. */
        val clientboundSelectKnownPacks: Int = 14,
        val serverboundClientInformation: Int = 0,
        val serverboundKeepAlive: Int = 4,
        val serverboundFinish: Int = 3,
        val serverboundSelectKnownPacks: Int = 7,
    )

    data class PlayIds(
        /** Where a client with no configuration phase sends its own settings. */
        val serverboundClientInformation: Int = 14,
        val clientboundKeepAlive: Int = 44,
        val clientboundPlayerPosition: Int = 72,
        val clientboundDisconnect: Int = 32,
        val serverboundKeepAlive: Int = 28,
        val serverboundAcceptTeleportation: Int = 0,
        val serverboundMovePosRot: Int = 31,
    )
}

/**
 * Every version this tool can speak.
 *
 * 26.1 and 26.2 share every id a bot uses: 26.2 moved no packet at all, it only renumbered
 * registries, and a bot never sends a block or an item. That is exactly why 26.1 is the cheapest
 * useful test of a server's translation, and why it is here first.
 */
object Protocols {
    val v26_2: ProtocolProfile = ProtocolProfile(776, "26.2")
    val v26_1: ProtocolProfile = ProtocolProfile(775, "26.1")

    /** 1.21.11 moved the play ids down by one, and nothing a bot sends changed shape. */
    val v1_21_11: ProtocolProfile =
        ProtocolProfile(
            774,
            "1.21.11",
            play = ProtocolProfile.PlayIds(
                clientboundKeepAlive = 43,
                clientboundPlayerPosition = 70,
                clientboundDisconnect = 32,
                serverboundKeepAlive = 27,
                serverboundMovePosRot = 30,
            ),
        )

    val v1_21_10: ProtocolProfile = v1_21_11.copy(protocol = 773, label = "1.21.10")

    /** Before 1.21.9's five new debug packets, so the play ids sit lower again. */
    val v1_21_8: ProtocolProfile =
        ProtocolProfile(
            772,
            "1.21.8",
            play = ProtocolProfile.PlayIds(
                clientboundKeepAlive = 38,
                clientboundPlayerPosition = 65,
                clientboundDisconnect = 28,
                serverboundKeepAlive = 27,
                serverboundMovePosRot = 30,
            ),
        )

    val v1_21_6: ProtocolProfile = v1_21_8.copy(protocol = 771, label = "1.21.6")

    /** 1.21.6 added the dialog packets, so before it the serverbound play ids are one lower. */
    val v1_21_5: ProtocolProfile =
        ProtocolProfile(
            770,
            "1.21.5",
            play = ProtocolProfile.PlayIds(
                clientboundKeepAlive = 38,
                clientboundPlayerPosition = 65,
                clientboundDisconnect = 28,
                serverboundKeepAlive = 26,
                serverboundMovePosRot = 29,
            ),
        )

    /** 1.21.5 added the test-instance packets, so before it the play ids sit one lower again. */
    val v1_21_4: ProtocolProfile =
        ProtocolProfile(
            769,
            "1.21.4",
            play = ProtocolProfile.PlayIds(
                clientboundKeepAlive = 39,
                clientboundPlayerPosition = 66,
                clientboundDisconnect = 29,
                serverboundKeepAlive = 26,
                serverboundMovePosRot = 29,
            ),
        )

    /** 1.21.2 and 1.21.3 share their numbering with 1.21.4. */
    val v1_21_3: ProtocolProfile = v1_21_4.copy(protocol = 768, label = "1.21.3")

    /**
     * 1.21 and 1.21.1, where the play ids are lower again and a bot has to send one field less in
     * its settings: the particle setting arrived in 1.21.2.
     */
    val v1_21: ProtocolProfile =
        ProtocolProfile(
            767,
            "1.21",
            play = ProtocolProfile.PlayIds(
                clientboundKeepAlive = 38,
                clientboundPlayerPosition = 64,
                clientboundDisconnect = 29,
                serverboundKeepAlive = 24,
                serverboundMovePosRot = 27,
            ),
            settingsHaveParticleStatus = false,
            positionStartsWithTeleportId = false,
        )

    /** 1.20.5 and 1.20.6, which number the play packets the same way 1.21 does. */
    val v1_20_6: ProtocolProfile = v1_21.copy(protocol = 766, label = "1.20.6")

    /** 1.20.3 and 1.20.4: no known packs to answer, and the configuration ids sit lower. */
    val v1_20_4: ProtocolProfile =
        ProtocolProfile(
            765,
            "1.20.4",
            configuration = ProtocolProfile.ConfigurationIds(
                clientboundDisconnect = 1,
                clientboundKeepAlive = 3,
                clientboundFinish = 2,
                clientboundSelectKnownPacks = -1,
                serverboundClientInformation = 0,
                serverboundKeepAlive = 3,
                serverboundFinish = 2,
                serverboundSelectKnownPacks = -1,
            ),
            play = ProtocolProfile.PlayIds(
                clientboundKeepAlive = 36,
                clientboundPlayerPosition = 62,
                clientboundDisconnect = 27,
                serverboundKeepAlive = 21,
                serverboundMovePosRot = 24,
            ),
            settingsHaveParticleStatus = false,
            positionStartsWithTeleportId = false,
        )

    /** 1.20.2, the first version with a configuration phase at all. */
    val v1_20_2: ProtocolProfile =
        v1_20_4.copy(
            protocol = 764,
            label = "1.20.2",
            play = ProtocolProfile.PlayIds(
                clientboundKeepAlive = 36,
                clientboundPlayerPosition = 62,
                clientboundDisconnect = 27,
                serverboundKeepAlive = 20,
                serverboundMovePosRot = 23,
            ),
        )

    /** 1.20 and 1.20.1: no configuration phase, and the settings are a play packet. */
    val v1_20_1: ProtocolProfile =
        ProtocolProfile(
            763,
            "1.20.1",
            play = ProtocolProfile.PlayIds(
                serverboundClientInformation = 8,
                clientboundKeepAlive = 35,
                clientboundPlayerPosition = 60,
                clientboundDisconnect = 26,
                serverboundKeepAlive = 18,
                serverboundMovePosRot = 21,
            ),
            hasConfigurationPhase = false,
            settingsHaveParticleStatus = false,
            positionStartsWithTeleportId = false,
        )

    /** 1.19.4, which numbers the play packets the same way 1.20 does. */
    val v1_19_4: ProtocolProfile = v1_20_1.copy(protocol = 762, label = "1.19.4")

    /** 1.19.3, where the play ids sit lower again. */
    val v1_19_3: ProtocolProfile =
        v1_19_4.copy(
            protocol = 761,
            label = "1.19.3",
            play = ProtocolProfile.PlayIds(
                serverboundClientInformation = 7,
                clientboundKeepAlive = 31,
                clientboundPlayerPosition = 56,
                clientboundDisconnect = 23,
                serverboundKeepAlive = 17,
                serverboundMovePosRot = 20,
            ),
        )

    /** 1.19.1 and 1.19.2, before the tab list was split into three packets. */
    val v1_19_2: ProtocolProfile =
        v1_19_3.copy(
            protocol = 760,
            label = "1.19.2",
            play = ProtocolProfile.PlayIds(
                serverboundClientInformation = 8,
                clientboundKeepAlive = 32,
                clientboundPlayerPosition = 57,
                clientboundDisconnect = 25,
                serverboundKeepAlive = 18,
                serverboundMovePosRot = 21,
            ),
        )

    /** 1.19, before the chat rework of 1.19.1. */
    val v1_19: ProtocolProfile =
        v1_19_2.copy(
            protocol = 759,
            label = "1.19",
            play = ProtocolProfile.PlayIds(
                serverboundClientInformation = 7,
                clientboundKeepAlive = 30,
                clientboundPlayerPosition = 54,
                clientboundDisconnect = 23,
                serverboundKeepAlive = 17,
                serverboundMovePosRot = 20,
            ),
        )

    /** 1.18.2, before everything alive spawned with the same packet as everything else. */
    val v1_18_2: ProtocolProfile =
        v1_19.copy(
            protocol = 758,
            label = "1.18.2",
            play = ProtocolProfile.PlayIds(
                serverboundClientInformation = 5,
                clientboundKeepAlive = 33,
                clientboundPlayerPosition = 56,
                clientboundDisconnect = 26,
                serverboundKeepAlive = 15,
                serverboundMovePosRot = 18,
            ),
        )

    /** 1.18 and 1.18.1, numbered like 1.18.2. */
    val v1_18_1: ProtocolProfile = v1_18_2.copy(protocol = 757, label = "1.18.1")

    /** 1.17.1, where a chunk is still a bitmask and a list of the sections that have anything in them. */
    val v1_17_1: ProtocolProfile = v1_18_1.copy(protocol = 756, label = "1.17.1", settingsHaveServerListing = false)

    /** 1.17, the oldest this server translates. */
    val v1_17: ProtocolProfile = v1_17_1.copy(protocol = 755, label = "1.17")

    /** 1.16.4 and 1.16.5, where the world is sixteen sections and no more. */
    val v1_16_5: ProtocolProfile =
        v1_17.copy(
            protocol = 754,
            label = "1.16.5",
            settingsHaveTextFiltering = false,
            play = ProtocolProfile.PlayIds(
                serverboundClientInformation = 5,
                clientboundKeepAlive = 31,
                clientboundPlayerPosition = 52,
                clientboundDisconnect = 25,
                serverboundKeepAlive = 16,
                serverboundMovePosRot = 19,
            ),
        )

    /** 1.16.3 and 1.16.2, which are 1.16.5 with other numbers on the wire. */
    val v1_16_3: ProtocolProfile = v1_16_5.copy(protocol = 753, label = "1.16.3")
    val v1_16_2: ProtocolProfile = v1_16_5.copy(protocol = 751, label = "1.16.2")


    /** 1.16.1 and 1.16, the last that are told what their dimensions are. */
    val v1_16_1: ProtocolProfile =
        v1_16_2.copy(
            protocol = 736,
            label = "1.16.1",
            play =
                ProtocolProfile.PlayIds(
                    serverboundClientInformation = 5,
                    clientboundKeepAlive = 32,
                    clientboundPlayerPosition = 53,
                    clientboundDisconnect = 26,
                    serverboundKeepAlive = 16,
                    serverboundMovePosRot = 19,
                ),
        )

    val v1_16: ProtocolProfile = v1_16_1.copy(protocol = 735, label = "1.16")

    /** 1.15.2, where a dimension is a number and there are three of them. */
    val v1_15_2: ProtocolProfile =
        v1_16_2.copy(
            protocol = 578,
            label = "1.15.2",
            play =
                ProtocolProfile.PlayIds(
                    serverboundClientInformation = 5,
                    clientboundKeepAlive = 33,
                    clientboundPlayerPosition = 54,
                    clientboundDisconnect = 27,
                    serverboundKeepAlive = 15,
                    serverboundMovePosRot = 18,
                ),
        )

    val v1_15_1: ProtocolProfile = v1_15_2.copy(protocol = 575, label = "1.15.1")
    val v1_15: ProtocolProfile = v1_15_2.copy(protocol = 573, label = "1.15")

    /** 1.14.4, where a biome is a column and lives inside the chunk. */
    val v1_14_4: ProtocolProfile =
        v1_16_2.copy(
            protocol = 498,
            label = "1.14.4",
            play =
                ProtocolProfile.PlayIds(
                    serverboundClientInformation = 5,
                    clientboundKeepAlive = 32,
                    clientboundPlayerPosition = 53,
                    clientboundDisconnect = 26,
                    serverboundKeepAlive = 15,
                    serverboundMovePosRot = 18,
                ),
        )

    val v1_14_3: ProtocolProfile = v1_14_4.copy(protocol = 490, label = "1.14.3")
    val v1_14_1: ProtocolProfile = v1_14_4.copy(protocol = 480, label = "1.14.1")
    val v1_14: ProtocolProfile = v1_14_4.copy(protocol = 477, label = "1.14")

    /** 1.13.2, where the light is part of the chunk. */
    val v1_13_2: ProtocolProfile =
        v1_16_2.copy(
            protocol = 404,
            label = "1.13.2",
            play =
                ProtocolProfile.PlayIds(
                    serverboundClientInformation = 4,
                    clientboundKeepAlive = 33,
                    clientboundPlayerPosition = 50,
                    clientboundDisconnect = 27,
                    serverboundKeepAlive = 14,
                    serverboundMovePosRot = 17,
                ),
        )

    val v1_13_1: ProtocolProfile = v1_13_2.copy(protocol = 401, label = "1.13.1")
    val v1_13: ProtocolProfile = v1_13_2.copy(protocol = 393, label = "1.13")

    /** 1.12.2, below the flattening: a block is an id and four bits of metadata. */
    val v1_12_2: ProtocolProfile =
        v1_16_2.copy(
            protocol = 340,
            label = "1.12.2",
            play =
                ProtocolProfile.PlayIds(
                    serverboundClientInformation = 4,
                    clientboundKeepAlive = 31,
                    clientboundPlayerPosition = 47,
                    clientboundDisconnect = 26,
                    serverboundKeepAlive = 11,
                    serverboundMovePosRot = 14,
                ),
        )

    val v1_12_1: ProtocolProfile =
        v1_16_2.copy(
            protocol = 338,
            label = "1.12.1",
            play =
                ProtocolProfile.PlayIds(
                    serverboundClientInformation = 4,
                    clientboundKeepAlive = 31,
                    clientboundPlayerPosition = 47,
                    clientboundDisconnect = 26,
                    serverboundKeepAlive = 11,
                    serverboundMovePosRot = 14,
                ),
        )

    val v1_12: ProtocolProfile =
        v1_16_2.copy(
            protocol = 335,
            label = "1.12",
            play =
                ProtocolProfile.PlayIds(
                    serverboundClientInformation = 5,
                    clientboundKeepAlive = 31,
                    clientboundPlayerPosition = 46,
                    clientboundDisconnect = 26,
                    serverboundKeepAlive = 12,
                    serverboundMovePosRot = 15,
                ),
        )

    /** 1.11 and 1.10, which number a bot the same way and differ elsewhere. */
    val v1_11_2: ProtocolProfile =
        v1_16_2.copy(
            protocol = 316,
            label = "1.11.2",
            play =
                ProtocolProfile.PlayIds(
                    serverboundClientInformation = 4,
                    clientboundKeepAlive = 31,
                    clientboundPlayerPosition = 46,
                    clientboundDisconnect = 26,
                    serverboundKeepAlive = 11,
                    serverboundMovePosRot = 13,
                ),
        )

    val v1_11: ProtocolProfile = v1_11_2.copy(protocol = 315, label = "1.11")
    val v1_10_2: ProtocolProfile = v1_11_2.copy(protocol = 210, label = "1.10.2")
    val v1_9_4: ProtocolProfile = v1_11_2.copy(protocol = 110, label = "1.9.4")
    val v1_9_2: ProtocolProfile = v1_11_2.copy(protocol = 109, label = "1.9.2")
    val v1_9: ProtocolProfile = v1_11_2.copy(protocol = 107, label = "1.9")

    /**
     * 1.8, where a position is not acknowledged and a client has one hand.
     *
     * The oldest version there is a client for that this tool can be: below it the login itself
     * changes shape.
     */
    val v1_8: ProtocolProfile =
        v1_9.copy(
            protocol = 47,
            label = "1.8",
            play =
                ProtocolProfile.PlayIds(
                    serverboundClientInformation = 21,
                    clientboundKeepAlive = 0,
                    clientboundPlayerPosition = 8,
                    clientboundDisconnect = 64,
                    serverboundKeepAlive = 0,
                    serverboundMovePosRot = 6,
                ),
            positionIsAcknowledged = false,
            settingsHaveMainHand = false,
        )
    val supported: List<ProtocolProfile> =
        listOf(
            v26_2, v26_1, v1_21_11, v1_21_10, v1_21_8, v1_21_6, v1_21_5, v1_21_4, v1_21_3, v1_21,
            v1_20_6, v1_20_4, v1_20_2, v1_20_1, v1_19_4, v1_19_3, v1_19_2, v1_19, v1_18_2, v1_18_1,
            v1_17_1, v1_17, v1_16_5, v1_16_3, v1_16_2, v1_16_1, v1_16, v1_15_2, v1_15_1, v1_15,
            v1_14_4, v1_14_3, v1_14_1, v1_14, v1_13_2, v1_13_1, v1_13, v1_12_2, v1_12_1, v1_12,
            v1_11_2, v1_11, v1_10_2, v1_9_4, v1_9_2, v1_9, v1_8,
        )

    val newest: ProtocolProfile = v26_2

    /** By name ("26.1") or by number ("775"), because both are how people refer to a version. */
    fun find(name: String): ProtocolProfile? {
        val trimmed = name.trim()
        supported.firstOrNull { it.label == trimmed }?.let { return it }
        val number = trimmed.toIntOrNull() ?: return null
        return supported.firstOrNull { it.protocol == number }
    }

    /**
     * A version this tool has no ids for, claimed anyway.
     *
     * The point is testing a server's door: what it does with a number it has never seen is worth
     * knowing, and finding out costs only the handshake. Anything past the handshake speaks the
     * newest ids, which is a guess and is meant to be one.
     */
    fun pretend(protocol: Int): ProtocolProfile = newest.copy(protocol = protocol, label = "unknown")
}
