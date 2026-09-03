#pragma once

#include <basalt/swarm/Bytes.hpp>
#include <basalt/swarm/Checks.hpp>
#include <basalt/swarm/Protocols.hpp>
#include <basalt/swarm/Socket.hpp>
#include <basalt/swarm/Spread.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <random>
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
    };

    explicit Bot(Options options);

    /// Connects, plays, and returns when the server hangs up or the bot is asked to stop.
    void run();

    /// Asks the bot to leave at the next turn of its loop.
    void stop() noexcept { running_ = false; }

    [[nodiscard]] std::int32_t problems() const noexcept { return checks_.problems(); }

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

    /// Takes complete frames out of `incoming_` and handles them. False ends the connection.
    [[nodiscard]] bool drainFrames();

    Options options_;
    TcpClient socket_;
    Checks checks_;
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
