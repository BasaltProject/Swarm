#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace basalt::swarm {

/// One version a bot can pretend to be: the handshake number and the packet
/// ids it implies, taken from the official jar's packet report.
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
    /// -1 where the version has no such packet, which is everything
    /// before 1.20.5.
    std::int32_t clientboundSelectKnownPacks = 14;
    std::int32_t serverboundClientInformation = 0;
    std::int32_t serverboundKeepAlive = 4;
    std::int32_t serverboundFinish = 3;
    std::int32_t serverboundSelectKnownPacks = 7;
    std::int32_t clientboundResourcePackPush = -1;
    std::int32_t serverboundResourcePackResponse = -1;
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
    /// Where the server announces an entity. -1 when unknown for the version:
    /// a bot then counts no entities rather than misreading another packet.
    std::int32_t clientboundAddEntity = -1;
    /// Where a client uses what it holds against a block face. -1 where this
    /// tool has not been told the number, and then `--place` refuses instead of
    /// sending it blind.
    std::int32_t serverboundUseItemOn = -1;
    /// Where a client right-clicks an entity. -1 when unknown, and then
    /// `--interact` refuses. 26.2 split the attack out of it, so the shape
    /// differs by version too, not only the number.
    std::int32_t serverboundInteract = -1;
    /// Where the server says something to everyone. -1 when unknown, and then
    /// a bot hears nothing rather than reading another packet as a sentence.
    std::int32_t clientboundSystemChat = -1;
    /// Where a client sends a command it typed, without the slash. -1 when unknown, and then `--run` refuses.
    std::int32_t serverboundChatCommand = -1;
    std::int32_t clientboundStartConfiguration = -1;
    std::int32_t serverboundConfigurationAcknowledged = -1;
    std::int32_t clientboundResourcePackPush = -1;
    std::int32_t serverboundResourcePackResponse = -1;
  };

  std::int32_t protocol = 0;
  std::string label;
  LoginIds login;
  ConfigurationIds configuration;
  PlayIds play;
  /// What a player looks like in the version's entity registry.
  std::int32_t playerEntityType = 156;
  /// Whether the version has a configuration phase (1.20.2 and later).
  bool hasConfigurationPhase = true;
  /// 1.21.2 added the particle setting to what a client tells the server about
  /// itself.
  bool settingsHaveParticleStatus = true;
  /// Whether a position has to be acknowledged (1.9 and later).
  bool positionIsAcknowledged = true;
  /// 1.9 asked which hand is the main one; 1.8 has one hand and does not say.
  bool settingsHaveMainHand = true;
  /// 1.17 added whether the client filters text, and 1.18 whether it wants to
  /// be listed.
  bool settingsHaveTextFiltering = true;
  bool settingsHaveServerListing = true;
  /// Whether the teleport id comes before the position, beside a delta
  /// movement (1.21.2 and later); before that the packet ends with the id.
  bool positionStartsWithTeleportId = true;
  /// 26.3 confirms a teleport with where the client landed, and sends no move
  /// after it.
  bool acceptCarriesPosition = false;
};

/// Every version this tool can speak. 26.1 and 26.2 share every id a bot
/// uses (26.2 only renumbered registries), which makes 26.1 the cheapest test
/// of a server's translation.
namespace protocols {

[[nodiscard]] std::span<const ProtocolProfile> supported();

[[nodiscard]] const ProtocolProfile &newest();

/// By name ("26.1") or by number ("775"), because both are how people refer to
/// a version.
[[nodiscard]] std::optional<ProtocolProfile> find(std::string_view name);

/// A version this tool has no ids for, claimed anyway, to see what a server
/// does with a number it has never seen. Past the handshake it speaks the
/// newest ids, as a deliberate guess.
[[nodiscard]] ProtocolProfile pretend(std::int32_t protocol);

} // namespace protocols

} // namespace basalt::swarm
