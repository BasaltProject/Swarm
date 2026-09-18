#pragma once

#include <basalt/swarm/Bytes.hpp>
#include <basalt/swarm/Checks.hpp>
#include <basalt/swarm/Protocols.hpp>
#include <basalt/swarm/Socket.hpp>
#include <basalt/swarm/Spread.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace basalt::swarm {

enum class ProtocolState : std::uint8_t { Login, Configuration, Play };

/// One bot: a socket, the state machine that gets it into the world, and the walk it does there.
///
/// Everything version-shaped comes from the profile, so a bot is the same code whether it claims to
/// be 1.8 or 26.2.
class Bot {
public:
    struct Options {
        std::int32_t id = 1;
        std::string host = "127.0.0.1";
        std::uint16_t port = 25565;
        bool physicsEnabled = false;
        std::optional<SpawnTarget> target;
        ProtocolProfile profile = protocols::newest();
        /// Where a bot's own lines go. One place, so a swarm can be made quiet.
        std::function<void(const std::string&)> say;
        /// Where the command tree goes, see `Checks::dumpCommandsTo`.
        std::string commandsDump;
        /// Where to put what is in hand, as `x,y,z`: the block is placed on the top face of that
        /// square, which is what a player standing beside it would do. Empty places nothing.
        std::string placeAt;
        /// How many blocks to place, one per go, each on top of the last.
        std::int32_t placeCount = 1;
        /// Which face of that square to use, in the protocol's own order: 0 down, 1 up, 2 north,
        /// 3 south, 4 west, 5 east. A side face is what hangs a painting or a torch.
        std::int32_t placeFace = 1;
        /// The entity type to right-click, as the protocol numbers them. Unset clicks nothing.
        std::optional<std::int32_t> interactType;
        /// How many times to click one, one entity per click.
        std::int32_t interactCount = 1;
        /// How long to wait between clicks, and before the first one: a bot that clicks on its
        /// first tick in the world clicks before whoever is driving it has set anything up.
        std::int32_t interactEveryMillis = 1000;
    };

    explicit Bot(Options options);

    /// Connects, plays, and returns when the server hangs up or the bot is asked to stop.
    void run();

    /// Asks the bot to leave at the next turn of its loop.
    void stop() noexcept { running_ = false; }

    [[nodiscard]] std::int32_t problems() const noexcept { return checks_.problems(); }

    /// How many other players this bot was shown. Zero on a server it is alone on, and zero is
    /// also what a server that never announces anybody looks like.
    [[nodiscard]] std::size_t playersSeen() const noexcept { return entitiesSeen_.size(); }

private:
    void say(const std::string& line) const;

    [[nodiscard]] bool sendPacket(std::int32_t packetId, const ByteWriter& body);

    [[nodiscard]] bool sendEmpty(std::int32_t packetId);

    [[nodiscard]] bool sendHandshakeAndLogin();

    [[nodiscard]] bool sendClientInformation();

    [[nodiscard]] bool sendPositionLook();

    /// One frame, already inflated. False means the bot is done with this connection.
    [[nodiscard]] bool handlePacket(std::int32_t packetId, ByteReader& body);

    [[nodiscard]] bool handleLoginPacket(std::int32_t packetId, ByteReader& body);

    [[nodiscard]] bool handleConfigurationPacket(std::int32_t packetId, ByteReader& body);

    [[nodiscard]] bool handlePlayPacket(std::int32_t packetId, ByteReader& body);

    /// A tick of the walk: wander, jump, fall, and tell the server where that left it.
    void tickPhysics();

    /// Right-clicks the nearest entity of the asked-for type, once per tick at most.
    void tickInteract();

    /// Puts what is in hand on the square that was asked for, one block per go.
    void tickPlace();

    /// Takes complete frames out of `incoming_` and handles them. False ends the connection.
    [[nodiscard]] bool drainFrames();

    Options options_;
    TcpClient socket_;
    Checks checks_;
    /// The entity ids of the other players this bot has been shown, so each is said once.
    std::set<std::int32_t> entitiesSeen_;

    /// Where each entity was when the server announced it. A bot is not told about movement, so a
    /// wandering animal drifts away from what is written here: `--interact` is for penned ones.
    struct SeenEntity {
        std::int32_t type = 0;
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };

    std::map<std::int32_t, SeenEntity> entities_;
    std::int32_t interactsSent_ = 0;
    std::int64_t nextInteractAt_ = 0;
    std::int32_t placesSent_ = 0;
    std::int64_t nextPlaceAt_ = 0;
    /// What the next block change this bot asks for is numbered. parity: the client's own counter,
    /// which the server echoes back in its acknowledgement.
    std::int32_t sequence_ = 0;
    /// Every line the server said to this bot, in order.
    std::vector<std::string> heard_;
    ProtocolState state_ = ProtocolState::Login;
    std::int32_t compressionThreshold_ = -1;
    bool running_ = true;

    std::vector<std::uint8_t> incoming_;

    double x_ = 0.0;
    double y_ = 0.0;
    double z_ = 0.0;
    float yaw_ = 0.0F;
    float pitch_ = 0.0F;
    double yVelocity_ = 0.0;
    bool wandering_ = false;
    std::int32_t wanderTicks_ = 0;
    bool onGround_ = false;
    bool positionReceived_ = false;

    std::mt19937 random_;
};

} // namespace basalt::swarm
