#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace basalt::swarm {

/// One version a bot can pretend to be: the number it sends in the handshake, and the packet ids
/// that number implies. They live here because they are the only thing that changes between
/// versions, so adding one is adding an entry taken from the official jar's own packet report.
struct ProtocolProfile {
    struct LoginIds {
        std::int32_t clientboundDisconnect = 0;
        std::int32_t clientboundCompression = 3;
        std::int32_t clientboundFinished = 2;
        std::int32_t serverboundHello = 0;
        std::int32_t serverboundAcknowledged = 3;
    };

    struct ConfigurationIds {
        std::int32_t clientboundDisconnect = 2;
        std::int32_t clientboundKeepAlive = 4;
        std::int32_t clientboundFinish = 3;
        /// -1 where the version has no such packet, which is everything before 1.20.5.
        std::int32_t clientboundSelectKnownPacks = 14;
        std::int32_t serverboundClientInformation = 0;
        std::int32_t serverboundKeepAlive = 4;
        std::int32_t serverboundFinish = 3;
        std::int32_t serverboundSelectKnownPacks = 7;
    };

    struct PlayIds {
        /// Where a client with no configuration phase sends its own settings.
        std::int32_t serverboundClientInformation = 14;
        std::int32_t clientboundKeepAlive = 44;
        std::int32_t clientboundPlayerPosition = 72;
        std::int32_t clientboundDisconnect = 32;
        std::int32_t serverboundKeepAlive = 28;
        std::int32_t serverboundAcceptTeleportation = 0;
        std::int32_t serverboundMovePosRot = 31;
        /// Where the server announces an entity. -1 where this tool has not been told the number
        /// for that version, and then a bot counts no entities rather than counting the wrong
        /// packet: the id moves almost every version, and guessing it reads a chunk as a spawn.
        std::int32_t clientboundAddEntity = -1;
    };

    /// What a player looks like in the entity registry of the versions this bot names ids for.
    static constexpr std::int32_t kPlayerEntityType = 156;

    std::int32_t protocol = 0;
    std::string label;
    LoginIds login;
    ConfigurationIds configuration;
    PlayIds play;
    /// Whether the version has a configuration phase at all.
    ///
    /// It arrived in 1.20.2. Before that a client goes from logging in straight to playing, and
    /// there is nothing to acknowledge on the way.
    bool hasConfigurationPhase = true;
    /// 1.21.2 added the particle setting to what a client tells the server about itself.
    bool settingsHaveParticleStatus = true;
    /// Whether a position has to be acknowledged at all.
    ///
    /// The teleport id and the packet that confirms it are 1.9's. A 1.8 client is simply moved and
    /// says nothing back.
    bool positionIsAcknowledged = true;
    /// 1.9 asked which hand is the main one; 1.8 has one hand and does not say.
    bool settingsHaveMainHand = true;
    /// 1.17 added whether the client filters text, and 1.18 whether it wants to be listed.
    bool settingsHaveTextFiltering = true;
    bool settingsHaveServerListing = true;
    /// Whether the teleport id comes before the position rather than after it.
    ///
    /// 1.21.2 moved it to the front and put a delta movement beside the position; before that the
    /// packet ends with the id.
    bool positionStartsWithTeleportId = true;
};

/// Every version this tool can speak.
///
/// 26.1 and 26.2 share every id a bot uses: 26.2 moved no packet at all, it only renumbered
/// registries, and a bot never sends a block or an item. That is exactly why 26.1 is the cheapest
/// useful test of a server's translation, and why it is here first.
namespace protocols {

[[nodiscard]] std::span<const ProtocolProfile> supported();

[[nodiscard]] const ProtocolProfile& newest();

/// By name ("26.1") or by number ("775"), because both are how people refer to a version.
[[nodiscard]] std::optional<ProtocolProfile> find(std::string_view name);

/// A version this tool has no ids for, claimed anyway.
///
/// The point is testing a server's door: what it does with a number it has never seen is worth
/// knowing, and finding out costs only the handshake. Anything past the handshake speaks the newest
/// ids, which is a guess and is meant to be one.
[[nodiscard]] ProtocolProfile pretend(std::int32_t protocol);

} // namespace protocols

} // namespace basalt::swarm
