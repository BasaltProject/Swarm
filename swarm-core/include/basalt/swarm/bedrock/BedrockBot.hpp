#pragma once

#include <basalt/swarm/bedrock/BedrockCrypto.hpp>
#include <basalt/swarm/bedrock/BedrockTypes.hpp>
#include <basalt/swarm/bedrock/RakClient.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace basalt::swarm::bedrock {

/// One Bedrock bot: the RakNet session, the login, and the walk to the world. A second
/// implementation on purpose, like the Java bot: nothing is shared with the server it points at, so
/// the two cannot agree on the same mistake.
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
        /// A command the bot runs once it is in, without its slash, or nothing to run none.
        std::string commandLine;
        /// A block to break once it is in, as x,y,z, or nothing to break none.
        std::string digAt;
        /// A block to click with what is in hand, as x,y,z, or nothing to click none.
        std::string putAt;
        /// Two slots of the carried 36 to trade places, as a,b, or a,b,n to carry n of them
        /// through the cursor. Nothing moves none.
        std::string swapSlots;
        /// The same, but the second slot is in the block whose window is open: a,b,n.
        std::string stashSlots;
        /// The other way round: a,b,n takes from the open block's slot a into carried slot b.
        std::string takeSlots;
        /// Whether this bot crouches, which is what makes a click place against a block instead of
        /// using it.
        bool sneaking = false;
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

    /// Everybody else the server said is here, and everybody else it drew. A server that presents
    /// nobody is a server nobody can play on together, which is worth reporting on its own.
    [[nodiscard]] const std::map<std::string, std::string>& listed() const noexcept { return listed_; }

    [[nodiscard]] const std::map<std::uint64_t, std::string>& seen() const noexcept { return seen_; }

    /// How many sounds the server played for this bot.
    [[nodiscard]] std::int32_t sounds() const noexcept { return sounds_; }

    /// How many particles the server drew for this bot.
    [[nodiscard]] std::int32_t particles() const noexcept { return particles_; }

    /// How many mob effects the server started, changed or took away.
    [[nodiscard]] std::int32_t effects() const noexcept { return effects_; }

    /// How many times the server said this bot, or somebody beside it, had been hit.
    [[nodiscard]] std::int32_t hits() const noexcept { return hits_; }

    /// How many item names the server declared, which is the palette a window is read against.
    [[nodiscard]] std::int32_t itemNames() const noexcept { return itemNames_; }

    /// How many stacks the server put in this bot's windows.
    [[nodiscard]] std::int32_t carried() const noexcept { return carried_; }

    /// How many single blocks the server said had changed.
    [[nodiscard]] std::int32_t blockChanges() const noexcept { return blockChanges_; }

    /// How many columns arrived with any blocks in them at all, as opposed to holes.
    [[nodiscard]] std::int32_t columnsWithBlocks() const noexcept { return columnsWithBlocks_; }

    /// Whether this bot was shown a death screen at any point.
    [[nodiscard]] bool died() const noexcept { return died_; }

    /// Every command the server said this client may type. A Bedrock client will not send what is
    /// not in here, so an empty set is a client that can run nothing.
    [[nodiscard]] const std::set<std::string>& offered() const noexcept { return offered_; }

    /// The last value of every attribute the server sent for this bot itself.
    [[nodiscard]] const std::map<std::string, float>& attributes() const noexcept { return attributes_; }

private:
    void say(const std::string& line) const;
    void problem(const std::string& line);

    void send(Packet packet, std::span<const std::uint8_t> body);
    [[nodiscard]] bool pump(std::int32_t millis);
    void handle(Packet packet, ByteReader& reader);
    void sendLogin();
    /// What this client answers a resource pack question with, in the shape its protocol reads.
    void sendPackResponse(std::uint8_t status, const std::string& name);

    void sendChat(const std::string& line);
    void sendCommand(const std::string& line);
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
    std::int32_t advertisedProtocol_ = 0;
    std::uint64_t runtimeEntityId_ = 0;
    float x_ = 0.0F;
    float y_ = 0.0F;
    float z_ = 0.0F;
    std::uint64_t tick_ = 0;

    /// The one block action or click still to send, which goes out with the next input.
    bool digSent_ = false;
    bool putSent_ = false;
    bool swapSent_ = false;
    /// The window this bot has open, or -1 when it has none.
    std::int32_t openContainer_ = -1;
    /// Which request number the next one gets. A client counts its own and the answer names it.
    std::int32_t requestId_ = 0;
    /// Who the server listed, by uuid, and who it actually drew, by entity id.
    std::map<std::string, std::string> listed_;
    std::map<std::uint64_t, std::string> seen_;
    std::map<std::string, float> attributes_;
    std::set<std::string> offered_;
    bool died_ = false;
    std::int32_t columnsWithBlocks_ = 0;
    std::int32_t blockChanges_ = 0;
    std::int32_t sounds_ = 0;
    std::int32_t particles_ = 0;
    std::int32_t effects_ = 0;
    std::int32_t hits_ = 0;
    std::set<std::int32_t> unread_;
    std::int32_t itemNames_ = 0;
    std::int32_t carried_ = 0;
};

} // namespace basalt::swarm::bedrock
