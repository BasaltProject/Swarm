#pragma once

#include <basalt/swarm/bedrock/BedrockCrypto.hpp>
#include <basalt/swarm/bedrock/BedrockTypes.hpp>
#include <basalt/swarm/bedrock/RakClient.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace basalt::swarm::bedrock {

/// The Bedrock protocol this bot claims. A server that speaks another answers
/// with the number it wants instead of letting the bot in, and that is a thing
/// worth testing too.
inline constexpr int kBedrockProtocol = 2168;

inline constexpr std::string_view kBedrockVersion = "1.26.40";

/// One Bedrock bot: the RakNet session, the login, and the walk to the world.
///
/// It is a second implementation of the protocol on purpose, like the Java bot:
/// every byte it writes is written against the specification and none of it is
/// shared with the server it points at, so the two cannot agree on the same
/// mistake.
class BedrockBot {
public:
    struct Options {
        std::int32_t id = 1;
        std::string host = "127.0.0.1";
        std::uint16_t port = 19132;
        int protocol = kBedrockProtocol;
        /// How long to stay once it is in, in milliseconds. Zero leaves as soon as
        /// it has spawned.
        std::int32_t stayMillis = 5000;
        /// What the bot says in chat once it is in, or nothing to stay quiet.
        std::string chatLine;
        std::function<void(const std::string&)> say;
    };

    explicit BedrockBot(Options options);
    ~BedrockBot();

    /// Connects, logs in, and returns when it has spawned, been refused, or given
    /// up.
    void run();

    void stop() noexcept { running_ = false; }

    [[nodiscard]] std::int32_t problems() const noexcept { return problems_; }

    [[nodiscard]] bool spawned() const noexcept { return spawned_; }

    /// How many of each packet arrived, by id, for a caller that wants to say
    /// what was missing.
    [[nodiscard]] const std::map<std::uint16_t, std::int32_t>& counts() const noexcept { return counts_; }

private:
    void say(const std::string& line) const;
    void problem(const std::string& line);

    void send(Packet packet, std::span<const std::uint8_t> body);
    [[nodiscard]] bool pump(std::int32_t millis);
    void handle(Packet packet, ByteReader& reader);
    void sendLogin();
    void sendChat(const std::string& line);
    void sendAuthInput();
    void onHandshake(ByteReader& reader);

    Options options_;
    RakClient rak_;
    std::unique_ptr<KeyPair> keys_;
    std::unique_ptr<Cipher> cipher_;
    bool compression_ = false;
    bool running_ = true;
    bool spawned_ = false;
    bool refused_ = false;
    std::int32_t problems_ = 0;
    std::map<std::uint16_t, std::int32_t> counts_;
    std::uint64_t runtimeEntityId_ = 0;
    float x_ = 0.0F;
    float y_ = 0.0F;
    float z_ = 0.0F;
    std::uint64_t tick_ = 0;
};

} // namespace basalt::swarm::bedrock
