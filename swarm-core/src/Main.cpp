#include <basalt/swarm/Bot.hpp>
#include <basalt/swarm/Protocols.hpp>
#include <basalt/swarm/Spread.hpp>
#include <basalt/swarm/bedrock/BedrockBot.hpp>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace basalt::swarm;

namespace {

/// One lock around standard output: a hundred bots writing at once otherwise interleave mid-word.
std::mutex sayLock;

void say(const std::string& line) {
    const std::lock_guard<std::mutex> guard(sayLock);
    // Flushed rather than left to the buffer: a run's whole output is progress, and piped into a
    // file it otherwise says nothing until the swarm ends.
    std::cout << line << std::endl;
}

template <typename T>
[[nodiscard]] std::optional<T> numberOf(const std::string_view text) {
    T value{};
    if (std::from_chars(text.data(), text.data() + text.size(), value).ec != std::errc{}) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<double> doubleOf(const std::string_view text) {
    try {
        return std::stod(std::string{text});
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

struct Arguments {
    std::string host = "127.0.0.1";
    std::uint16_t port = 25565;
    std::int32_t count = 1;
    std::int64_t delayMillis = 200;
    bool physicsEnabled = false;
    double spread = 0.0;
    ProtocolProfile profile = protocols::newest();
    /// Which edition the swarm arrives on. Bedrock is another protocol on another transport, so it
    /// is a different bot and not another profile of the same one.
    bool bedrock = false;
    std::int32_t stayMillis = 5000;
    /// Which Bedrock protocols the bots claim, dealt out one per bot in turn. More than one is how
    /// a single run puts several versions on a server at the same time.
    std::vector<std::int32_t> bedrockProtocols{bedrock::kBedrockProtocol};
    std::string chatLine;
    std::string commandLine;
    bool ok = true;
};

/// The protocols a run is speaking, for the line it prints before it starts.
[[nodiscard]] std::string protocolList(const std::vector<std::int32_t>& protocols) {
    std::string text;
    for (const std::int32_t protocol : protocols) {
        text += (text.empty() ? "" : ", ") + std::to_string(protocol);
    }
    return text;
}

[[nodiscard]] Arguments parse(const std::vector<std::string_view>& args) {
    Arguments parsed;
    for (std::size_t i = 0; i < args.size();) {
        const std::string_view flag = args[i];
        const std::string_view next = i + 1 < args.size() ? args[i + 1] : std::string_view{};
        if (flag == "--ip") {
            const std::size_t colon = next.find(':');
            if (colon != std::string_view::npos) {
                parsed.host = std::string{next.substr(0, colon)};
                parsed.port = numberOf<std::uint16_t>(next.substr(colon + 1)).value_or(parsed.port);
            } else if (!next.empty()) {
                parsed.host = std::string{next};
            }
            i += 2;
        } else if (flag == "--count" || flag == "-c") {
            parsed.count = numberOf<std::int32_t>(next).value_or(parsed.count);
            i += 2;
        } else if (flag == "--delay" || flag == "-d") {
            parsed.delayMillis = numberOf<std::int64_t>(next).value_or(parsed.delayMillis);
            i += 2;
        } else if (flag == "--physics") {
            parsed.physicsEnabled = true;
            i++;
        } else if (flag == "--no-physics") {
            parsed.physicsEnabled = false;
            i++;
        } else if (flag == "--version" || flag == "--protocol") {
            if (const std::optional<ProtocolProfile> found = protocols::find(next); found.has_value()) {
                parsed.profile = *found;
            } else if (const std::optional<std::int32_t> number = numberOf<std::int32_t>(next); number.has_value()) {
                // A bare number this tool has no ids for is still worth claiming: it is how a
                // server's answer to an unknown version gets tested.
                parsed.profile = protocols::pretend(*number);
                say("No packet ids for protocol " + std::to_string(*number) + ": past the handshake this speaks " +
                    std::string{protocols::newest().label} + ".");
            } else {
                say("Unknown version: " + std::string{next});
                std::string known;
                for (const ProtocolProfile& profile : protocols::supported()) {
                    if (!known.empty()) {
                        known += ", ";
                    }
                    known += profile.label + " (" + std::to_string(profile.protocol) + ")";
                }
                say("Known: " + known);
                parsed.ok = false;
                return parsed;
            }
            i += 2;
        } else if (flag == "--bedrock") {
            parsed.bedrock = true;
            // Nobody writing --bedrock means the Java port, so the default moves with the flag.
            if (parsed.port == 25565) {
                parsed.port = 19132;
            }
            i++;
        } else if (flag == "--say") {
            parsed.chatLine = std::string{next};
            i += 2;
        } else if (flag == "--bedrock-protocol") {
            std::vector<std::int32_t> asked;
            for (std::size_t at = 0; at <= next.size();) {
                const std::size_t comma = std::min(next.find(',', at), next.size());
                const std::optional<std::int32_t> number = numberOf<std::int32_t>(next.substr(at, comma - at));
                if (number.has_value()) {
                    asked.push_back(*number);
                }
                at = comma + 1;
            }
            if (!asked.empty()) {
                parsed.bedrockProtocols = std::move(asked);
            }
            i += 2;
        } else if (flag == "--run") {
            parsed.commandLine = std::string{next};
            i += 2;
        } else if (flag == "--stay") {
            parsed.stayMillis = numberOf<std::int32_t>(next).value_or(parsed.stayMillis);
            i += 2;
        } else if (flag == "--spread") {
            parsed.spread = doubleOf(next).value_or(parsed.spread);
            i += 2;
        } else {
            i++;
        }
    }
    return parsed;
}

} // namespace

int main(int argc, char** argv) {
    say("Basalt Swarm starting...");
    const std::vector<std::string_view> args{argv + 1, argv + argc};
    const Arguments parsed = parse(args);
    if (!parsed.ok) {
        return 2;
    }

    const std::string layout = parsed.spread > 0.0
                                   ? std::to_string(static_cast<std::int64_t>(parsed.spread)) + " blocks apart"
                                   : "on the spawn";
    say("Target: " + parsed.host + ":" + std::to_string(parsed.port) + " | Bots: " + std::to_string(parsed.count) +
        " | Delay: " + std::to_string(parsed.delayMillis) +
        "ms | Physics: " + (parsed.physicsEnabled ? "true" : "false") + " | Layout: " + layout + " | Speaking: " +
        (parsed.bedrock
             ? "Bedrock " + std::string{bedrock::kBedrockVersion} + " (" + protocolList(parsed.bedrockProtocols) + ")"
             : std::string{parsed.profile.label} + " (" + std::to_string(parsed.profile.protocol) + ")"));

    if (parsed.bedrock) {
        std::vector<std::jthread> bedrockBots;
        bedrockBots.reserve(static_cast<std::size_t>(std::max(parsed.count, 0)));
        for (std::int32_t id = 1; id <= parsed.count; ++id) {
            bedrock::BedrockBot::Options options;
            options.id = id;
            options.host = parsed.host;
            options.port = parsed.port;
            options.stayMillis = parsed.stayMillis;
            options.protocol =
                parsed.bedrockProtocols[static_cast<std::size_t>(id - 1) % parsed.bedrockProtocols.size()];
            options.chatLine = parsed.chatLine;
            options.commandLine = parsed.commandLine;
            options.say = say;
            bedrockBots.emplace_back(
                [moved = std::move(options)]() mutable { bedrock::BedrockBot{std::move(moved)}.run(); });
            if (parsed.delayMillis > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds{parsed.delayMillis});
            }
        }
        return 0;
    }

    const std::vector<SpawnTarget> targets = spawnGrid(parsed.count, parsed.spread);
    std::vector<std::jthread> bots;
    bots.reserve(static_cast<std::size_t>(std::max(parsed.count, 0)));
    for (std::int32_t id = 1; id <= parsed.count; ++id) {
        Bot::Options options;
        options.id = id;
        options.host = parsed.host;
        options.port = parsed.port;
        options.physicsEnabled = parsed.physicsEnabled;
        options.profile = parsed.profile;
        options.say = say;
        if (static_cast<std::size_t>(id - 1) < targets.size()) {
            options.target = targets[static_cast<std::size_t>(id - 1)];
        }
        bots.emplace_back([moved = std::move(options)]() mutable { Bot{std::move(moved)}.run(); });
        if (parsed.delayMillis > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds{parsed.delayMillis});
        }
    }
    // Every bot is joined by its own destructor: the swarm ends when the last one is disconnected.
    return 0;
}
