#include <basalt/swarm/Protocols.hpp>

#include <algorithm>
#include <charconv>
#include <vector>

namespace basalt::swarm::protocols {
namespace {

using Play = ProtocolProfile::PlayIds;
using Configuration = ProtocolProfile::ConfigurationIds;

/// Take a version and change only what this one moved, which is how the table
/// stays readable: most versions differ from their neighbour in three numbers.
[[nodiscard]] ProtocolProfile like(const ProtocolProfile& base, const std::int32_t protocol, const std::string& label) {
    ProtocolProfile copy = base;
    copy.protocol = protocol;
    copy.label = label;
    return copy;
}

[[nodiscard]] ProtocolProfile like(const ProtocolProfile& base, const std::int32_t protocol, const std::string& label,
                                   const Play& play) {
    ProtocolProfile copy = like(base, protocol, label);
    copy.play = play;
    return copy;
}

[[nodiscard]] std::vector<ProtocolProfile> build() {
    // 26.3 inserted three clientbound packets and one serverbound, and a teleport
    // is confirmed with the position it landed on.
    ProtocolProfile v26_3{.protocol = 777,
                          .label = "26.3",
                          .login = {},
                          .configuration = Configuration{.clientboundSelectKnownPacks = 15,
                                                         .clientboundResourcePackPush = 9,
                                                         .serverboundResourcePackResponse = 6},
                          .play = Play{.clientboundKeepAlive = 45,
                                       .clientboundPlayerPosition = 73,
                                       .clientboundAddEntity = 1,
                                       .serverboundUseItemOn = 0x42,
                                       .serverboundInteract = 0x1a,
                                       .clientboundSystemChat = 124,
                                       .serverboundChatCommand = 7,
                                       .clientboundStartConfiguration = 0x78,
                                       .serverboundConfigurationAcknowledged = 0x10,
                                       .clientboundResourcePackPush = 0x51,
                                       .serverboundResourcePackResponse = 0x32,
                                       .clientboundShowDialog = 0x8f,
                                       .serverboundCustomClickAction = 0x44},
                          .playerEntityType = 159};
    v26_3.acceptCarriesPosition = true;

    // 26.1 and 26.2 number their packets identically: the jump between them moved
    // no packet at all, so what is written here for one holds for the other.
    const ProtocolProfile v26_2{.protocol = 776,
                                .label = "26.2",
                                .login = {},
                                .configuration = {},
                                .play = Play{.clientboundAddEntity = 1,
                                             .serverboundUseItemOn = 0x42,
                                             .serverboundInteract = 0x1a,
                                             .clientboundSystemChat = 121,
                                             .serverboundChatCommand = 7,
                                             .clientboundStartConfiguration = 118,
                                             .serverboundConfigurationAcknowledged = 16}};
    const ProtocolProfile v26_1 = like(v26_2, 775, "26.1");

    // 1.21.11 moved the play ids down by one, and nothing a bot sends changed
    // shape.
    const ProtocolProfile v1_21_11{.protocol = 774,
                                   .label = "1.21.11",
                                   .login = {},
                                   .configuration = {},
                                   .play = Play{.clientboundKeepAlive = 43,
                                                .clientboundPlayerPosition = 70,
                                                .clientboundDisconnect = 32,
                                                .serverboundKeepAlive = 27,
                                                .serverboundMovePosRot = 30,
                                                .clientboundSystemChat = 119,
                                                .clientboundStartConfiguration = 116,
                                                .serverboundConfigurationAcknowledged = 15}};
    const ProtocolProfile v1_21_10 = like(v1_21_11, 773, "1.21.10");

    // Before 1.21.9's five new debug packets, so the play ids sit lower again.
    const ProtocolProfile v1_21_8{.protocol = 772,
                                  .label = "1.21.8",
                                  .login = {},
                                  .configuration = {},
                                  .play = Play{.clientboundKeepAlive = 38,
                                               .clientboundPlayerPosition = 65,
                                               .clientboundDisconnect = 28,
                                               .serverboundKeepAlive = 27,
                                               .serverboundMovePosRot = 30,
                                               .clientboundSystemChat = 114,
                                               .clientboundStartConfiguration = 111,
                                               .serverboundConfigurationAcknowledged = 15}};
    const ProtocolProfile v1_21_6 = like(v1_21_8, 771, "1.21.6");

    // 1.21.6 added the dialog packets, so before it the serverbound play ids are
    // one lower.
    const ProtocolProfile v1_21_5{.protocol = 770,
                                  .label = "1.21.5",
                                  .login = {},
                                  .configuration = {},
                                  .play = Play{.clientboundKeepAlive = 38,
                                               .clientboundPlayerPosition = 65,
                                               .clientboundDisconnect = 28,
                                               .serverboundKeepAlive = 26,
                                               .serverboundMovePosRot = 29,
                                               .clientboundSystemChat = 114,
                                               .clientboundStartConfiguration = 111,
                                               .serverboundConfigurationAcknowledged = 14}};

    // 1.21.5 added the test-instance packets, so before it the play ids sit one
    // lower again.
    const ProtocolProfile v1_21_4{.protocol = 769,
                                  .label = "1.21.4",
                                  .login = {},
                                  .configuration = {},
                                  .play = Play{.clientboundKeepAlive = 39,
                                               .clientboundPlayerPosition = 66,
                                               .clientboundDisconnect = 29,
                                               .serverboundKeepAlive = 26,
                                               .serverboundMovePosRot = 29,
                                               .clientboundSystemChat = 115,
                                               .clientboundStartConfiguration = 112,
                                               .serverboundConfigurationAcknowledged = 14}};

    // 1.21.2 and 1.21.3 share their numbering with 1.21.4.
    const ProtocolProfile v1_21_3 = like(v1_21_4, 768, "1.21.3");

    // 1.21 and 1.21.1, where the play ids are lower again and a bot has to send
    // one field less in its settings: the particle setting arrived in 1.21.2.
    ProtocolProfile v1_21{.protocol = 767,
                          .label = "1.21",
                          .login = {},
                          .configuration = {},
                          .play = Play{.clientboundKeepAlive = 38,
                                       .clientboundPlayerPosition = 64,
                                       .clientboundDisconnect = 29,
                                       .serverboundKeepAlive = 24,
                                       .serverboundMovePosRot = 27,
                                       .clientboundSystemChat = 108,
                                       .clientboundStartConfiguration = 105,
                                       .serverboundConfigurationAcknowledged = 12}};
    v1_21.settingsHaveParticleStatus = false;
    v1_21.positionStartsWithTeleportId = false;

    // 1.20.5 and 1.20.6, which number the play packets the same way 1.21 does.
    const ProtocolProfile v1_20_6 = like(v1_21, 766, "1.20.6");

    // 1.20.3 and 1.20.4: no known packs to answer, and the configuration ids sit
    // lower.
    ProtocolProfile v1_20_4{.protocol = 765,
                            .label = "1.20.4",
                            .login = {},
                            .configuration = Configuration{.clientboundDisconnect = 1,
                                                           .clientboundKeepAlive = 3,
                                                           .clientboundPing = 4,
                                                           .serverboundPong = 4,
                                                           .clientboundFinish = 2,
                                                           .clientboundSelectKnownPacks = -1,
                                                           .serverboundClientInformation = 0,
                                                           .serverboundKeepAlive = 3,
                                                           .serverboundFinish = 2,
                                                           .serverboundSelectKnownPacks = -1},
                            .play = Play{.clientboundKeepAlive = 36,
                                         .clientboundPlayerPosition = 62,
                                         .clientboundDisconnect = 27,
                                         .serverboundKeepAlive = 21,
                                         .serverboundMovePosRot = 24,
                                         .clientboundSystemChat = 105,
                                         .clientboundStartConfiguration = 103,
                                         .serverboundConfigurationAcknowledged = 11}};
    v1_20_4.settingsHaveParticleStatus = false;
    v1_20_4.positionStartsWithTeleportId = false;

    // 1.20.2, the first version with a configuration phase at all.
    const ProtocolProfile v1_20_2 = like(v1_20_4, 764, "1.20.2",
                                         Play{.clientboundKeepAlive = 36,
                                              .clientboundPlayerPosition = 62,
                                              .clientboundDisconnect = 27,
                                              .serverboundKeepAlive = 20,
                                              .serverboundMovePosRot = 23,
                                              .clientboundStartConfiguration = 101,
                                              .serverboundConfigurationAcknowledged = 11});

    // 1.20 and 1.20.1: no configuration phase, and the settings are a play
    // packet.
    ProtocolProfile v1_20_1{.protocol = 763,
                            .label = "1.20.1",
                            .login = {},
                            .configuration = {},
                            .play = Play{.serverboundClientInformation = 8,
                                         .clientboundKeepAlive = 35,
                                         .clientboundPlayerPosition = 60,
                                         .clientboundDisconnect = 26,
                                         .serverboundKeepAlive = 18,
                                         .serverboundMovePosRot = 21,
                                         .clientboundSystemChat = 100}};
    v1_20_1.hasConfigurationPhase = false;
    v1_20_1.settingsHaveParticleStatus = false;
    v1_20_1.positionStartsWithTeleportId = false;

    // 1.19.4, which numbers the play packets the same way 1.20 does.
    const ProtocolProfile v1_19_4 = like(v1_20_1, 762, "1.19.4");

    // 1.19.3, where the play ids sit lower again.
    const ProtocolProfile v1_19_3 = like(v1_19_4, 761, "1.19.3",
                                         Play{.serverboundClientInformation = 7,
                                              .clientboundKeepAlive = 31,
                                              .clientboundPlayerPosition = 56,
                                              .clientboundDisconnect = 23,
                                              .serverboundKeepAlive = 17,
                                              .serverboundMovePosRot = 20});

    // 1.19.1 and 1.19.2, before the tab list was split into three packets.
    const ProtocolProfile v1_19_2 = like(v1_19_3, 760, "1.19.2",
                                         Play{.serverboundClientInformation = 8,
                                              .clientboundKeepAlive = 32,
                                              .clientboundPlayerPosition = 57,
                                              .clientboundDisconnect = 25,
                                              .serverboundKeepAlive = 18,
                                              .serverboundMovePosRot = 21});

    // 1.19, before the chat rework of 1.19.1.
    const ProtocolProfile v1_19 = like(v1_19_2, 759, "1.19",
                                       Play{.serverboundClientInformation = 7,
                                            .clientboundKeepAlive = 30,
                                            .clientboundPlayerPosition = 54,
                                            .clientboundDisconnect = 23,
                                            .serverboundKeepAlive = 17,
                                            .serverboundMovePosRot = 20});

    // 1.18.2, before everything alive spawned with the same packet as everything
    // else.
    const ProtocolProfile v1_18_2 = like(v1_19, 758, "1.18.2",
                                         Play{.serverboundClientInformation = 5,
                                              .clientboundKeepAlive = 33,
                                              .clientboundPlayerPosition = 56,
                                              .clientboundDisconnect = 26,
                                              .serverboundKeepAlive = 15,
                                              .serverboundMovePosRot = 18});

    // 1.18 and 1.18.1, numbered like 1.18.2.
    const ProtocolProfile v1_18_1 = like(v1_18_2, 757, "1.18.1");

    // 1.17.1, where a chunk is still a bitmask and a list of the sections that
    // have anything in them.
    ProtocolProfile v1_17_1 = like(v1_18_1, 756, "1.17.1");
    v1_17_1.settingsHaveServerListing = false;

    // 1.17, the oldest this server translates.
    const ProtocolProfile v1_17 = like(v1_17_1, 755, "1.17");

    // 1.16.4 and 1.16.5, where the world is sixteen sections and no more.
    ProtocolProfile v1_16_5 = like(v1_17, 754, "1.16.5",
                                   Play{.serverboundClientInformation = 5,
                                        .clientboundKeepAlive = 31,
                                        .clientboundPlayerPosition = 52,
                                        .clientboundDisconnect = 25,
                                        .serverboundKeepAlive = 16,
                                        .serverboundMovePosRot = 19});
    v1_16_5.settingsHaveTextFiltering = false;

    // 1.16.3 and 1.16.2, which are 1.16.5 with other numbers on the wire.
    const ProtocolProfile v1_16_3 = like(v1_16_5, 753, "1.16.3");
    const ProtocolProfile v1_16_2 = like(v1_16_5, 751, "1.16.2");

    // 1.16.1 and 1.16, the last that are told what their dimensions are.
    const ProtocolProfile v1_16_1 = like(v1_16_2, 736, "1.16.1",
                                         Play{.serverboundClientInformation = 5,
                                              .clientboundKeepAlive = 32,
                                              .clientboundPlayerPosition = 53,
                                              .clientboundDisconnect = 26,
                                              .serverboundKeepAlive = 16,
                                              .serverboundMovePosRot = 19});
    const ProtocolProfile v1_16 = like(v1_16_1, 735, "1.16");

    // 1.15.2, where a dimension is a number and there are three of them.
    const ProtocolProfile v1_15_2 = like(v1_16_2, 578, "1.15.2",
                                         Play{.serverboundClientInformation = 5,
                                              .clientboundKeepAlive = 33,
                                              .clientboundPlayerPosition = 54,
                                              .clientboundDisconnect = 27,
                                              .serverboundKeepAlive = 15,
                                              .serverboundMovePosRot = 18});
    const ProtocolProfile v1_15_1 = like(v1_15_2, 575, "1.15.1");
    const ProtocolProfile v1_15 = like(v1_15_2, 573, "1.15");

    // 1.14.4, where a biome is a column and lives inside the chunk.
    const ProtocolProfile v1_14_4 = like(v1_16_2, 498, "1.14.4",
                                         Play{.serverboundClientInformation = 5,
                                              .clientboundKeepAlive = 32,
                                              .clientboundPlayerPosition = 53,
                                              .clientboundDisconnect = 26,
                                              .serverboundKeepAlive = 15,
                                              .serverboundMovePosRot = 18});
    const ProtocolProfile v1_14_3 = like(v1_14_4, 490, "1.14.3");
    const ProtocolProfile v1_14_2 = like(v1_14_4, 485, "1.14.2");
    const ProtocolProfile v1_14_1 = like(v1_14_4, 480, "1.14.1");
    const ProtocolProfile v1_14 = like(v1_14_4, 477, "1.14");

    // 1.13.2, where the light is part of the chunk.
    const ProtocolProfile v1_13_2 = like(v1_16_2, 404, "1.13.2",
                                         Play{.serverboundClientInformation = 4,
                                              .clientboundKeepAlive = 33,
                                              .clientboundPlayerPosition = 50,
                                              .clientboundDisconnect = 27,
                                              .serverboundKeepAlive = 14,
                                              .serverboundMovePosRot = 17});
    const ProtocolProfile v1_13_1 = like(v1_13_2, 401, "1.13.1");
    const ProtocolProfile v1_13 = like(v1_13_2, 393, "1.13");

    // 1.12.2, below the flattening: a block is an id and four bits of metadata.
    const Play play1_12_2{.serverboundClientInformation = 4,
                          .clientboundKeepAlive = 31,
                          .clientboundPlayerPosition = 47,
                          .clientboundDisconnect = 26,
                          .serverboundKeepAlive = 11,
                          .serverboundMovePosRot = 14};
    const ProtocolProfile v1_12_2 = like(v1_16_2, 340, "1.12.2", play1_12_2);
    const ProtocolProfile v1_12_1 = like(v1_16_2, 338, "1.12.1", play1_12_2);
    const ProtocolProfile v1_12 = like(v1_16_2, 335, "1.12",
                                       Play{.serverboundClientInformation = 5,
                                            .clientboundKeepAlive = 31,
                                            .clientboundPlayerPosition = 46,
                                            .clientboundDisconnect = 26,
                                            .serverboundKeepAlive = 12,
                                            .serverboundMovePosRot = 15});

    // 1.11 and 1.10, which number a bot the same way and differ elsewhere.
    const ProtocolProfile v1_11_2 = like(v1_16_2, 316, "1.11.2",
                                         Play{.serverboundClientInformation = 4,
                                              .clientboundKeepAlive = 31,
                                              .clientboundPlayerPosition = 46,
                                              .clientboundDisconnect = 26,
                                              .serverboundKeepAlive = 11,
                                              .serverboundMovePosRot = 13});
    const ProtocolProfile v1_11 = like(v1_11_2, 315, "1.11");
    const ProtocolProfile v1_10_2 = like(v1_11_2, 210, "1.10.2");
    const ProtocolProfile v1_9_4 = like(v1_11_2, 110, "1.9.4");
    const ProtocolProfile v1_9_2 = like(v1_11_2, 109, "1.9.2");
    const ProtocolProfile v1_9_1 = like(v1_11_2, 108, "1.9.1");
    const ProtocolProfile v1_9 = like(v1_11_2, 107, "1.9");

    // 1.8, where a position is not acknowledged and a client has one hand.
    //
    // The oldest version there is a client for that this tool can be: below it
    // the login itself changes shape.
    ProtocolProfile v1_8 = like(v1_9, 47, "1.8",
                                Play{.serverboundClientInformation = 21,
                                     .clientboundKeepAlive = 0,
                                     .clientboundPlayerPosition = 8,
                                     .clientboundDisconnect = 64,
                                     .serverboundKeepAlive = 0,
                                     .serverboundMovePosRot = 6});
    v1_8.positionIsAcknowledged = false;
    v1_8.settingsHaveMainHand = false;

    return {v26_3,   v26_2,   v26_1,   v1_21_11, v1_21_10, v1_21_8, v1_21_6, v1_21_5, v1_21_4, v1_21_3,
            v1_21,   v1_20_6, v1_20_4, v1_20_2,  v1_20_1,  v1_19_4, v1_19_3, v1_19_2, v1_19,   v1_18_2,
            v1_18_1, v1_17_1, v1_17,   v1_16_5,  v1_16_3,  v1_16_2, v1_16_1, v1_16,   v1_15_2, v1_15_1,
            v1_15,   v1_14_4, v1_14_3, v1_14_2,  v1_14_1,  v1_14,   v1_13_2, v1_13_1, v1_13,   v1_12_2,
            v1_12_1, v1_12,   v1_11_2, v1_11,    v1_10_2,  v1_9_4,  v1_9_2,  v1_9_1,  v1_9,    v1_8};
}

} // namespace

std::span<const ProtocolProfile> supported() {
    static const std::vector<ProtocolProfile> kAll = build();
    return kAll;
}

const ProtocolProfile& newest() { return supported().front(); }

std::optional<ProtocolProfile> find(const std::string_view name) {
    const std::size_t first = name.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view trimmed = name.substr(first, name.find_last_not_of(" \t\r\n") - first + 1);
    const std::span<const ProtocolProfile> all = supported();
    const auto byLabel = std::ranges::find_if(all, [trimmed](const ProtocolProfile& p) { return p.label == trimmed; });
    if (byLabel != all.end()) {
        return *byLabel;
    }
    std::int32_t number = 0;
    if (std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), number).ec != std::errc{}) {
        return std::nullopt;
    }
    const auto byNumber =
        std::ranges::find_if(all, [number](const ProtocolProfile& p) { return p.protocol == number; });
    return byNumber == all.end() ? std::nullopt : std::optional{*byNumber};
}

ProtocolProfile pretend(const std::int32_t protocol) {
    ProtocolProfile guessed = newest();
    guessed.protocol = protocol;
    guessed.label = "unknown";
    return guessed;
}

} // namespace basalt::swarm::protocols
