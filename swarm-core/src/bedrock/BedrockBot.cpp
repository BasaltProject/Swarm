#include <basalt/swarm/Zlib.hpp>
#include <basalt/swarm/bedrock/BedrockBot.hpp>

#include <chrono>
#include <utility>

namespace basalt::swarm::bedrock {
namespace {

/// Under this many bytes a batch travels uncompressed with a byte saying so,
/// which is what the network settings agree on.
constexpr std::size_t kCompressionThreshold = 256;

constexpr std::uint32_t kPacketIdMask = 0x3ffU;

[[nodiscard]] std::int64_t nowMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

BedrockBot::BedrockBot(Options options) : options_(std::move(options)), rak_(options_.host, options_.port) {}

BedrockBot::~BedrockBot() = default;

void BedrockBot::say(const std::string& line) const {
    if (options_.say) {
        options_.say("[bedrock " + std::to_string(options_.id) + "] " + line);
    }
}

void BedrockBot::problem(const std::string& line) {
    ++problems_;
    say("PROBLEM: " + line);
}

void BedrockBot::send(const Packet packet, const std::span<const std::uint8_t> body) {
    ByteWriter encoded;
    writeVarUInt(encoded, static_cast<std::uint32_t>(packet));
    encoded.writeBytes(body);

    ByteWriter batch;
    writeVarUInt(batch, static_cast<std::uint32_t>(encoded.size()));
    batch.writeBytes(encoded.bytes());

    std::vector<std::uint8_t> payload;
    if (compression_) {
        if (batch.size() >= kCompressionThreshold) {
            const auto compressed = zlib::compressRaw(batch.bytes());
            if (!compressed.has_value()) {
                problem("could not compress a batch: " + compressed.error().message);
                return;
            }
            payload.push_back(0x00);
            payload.insert(payload.end(), compressed->begin(), compressed->end());
        } else {
            payload.push_back(0xff);
            payload.insert(payload.end(), batch.bytes().begin(), batch.bytes().end());
        }
    } else {
        payload.assign(batch.bytes().begin(), batch.bytes().end());
    }

    if (cipher_) {
        std::vector<std::uint8_t> encrypted;
        cipher_->encrypt(payload, encrypted);
        payload = std::move(encrypted);
    }
    rak_.sendGame(payload);
}

bool BedrockBot::pump(const std::int32_t millis) {
    const std::vector<std::vector<std::uint8_t>> payloads = rak_.poll(millis);
    for (const std::vector<std::uint8_t>& raw : payloads) {
        std::span<const std::uint8_t> batch = raw;
        std::vector<std::uint8_t> plain;
        if (cipher_) {
            if (!cipher_->decrypt(raw, plain)) {
                problem("a batch did not match its own checksum");
                return false;
            }
            batch = plain;
        }

        std::vector<std::uint8_t> inflated;
        if (compression_) {
            if (batch.empty()) {
                problem("an empty batch arrived where a compressed one was expected");
                return false;
            }
            const std::uint8_t algorithm = batch[0];
            batch = batch.subspan(1);
            if (algorithm == 0x00) {
                const auto result = zlib::decompressRaw(batch);
                if (!result.has_value()) {
                    problem("a batch would not inflate: " + result.error().message);
                    return false;
                }
                inflated = *result;
                batch = inflated;
            } else if (algorithm != 0xff) {
                problem("a batch arrived compressed with algorithm " + std::to_string(algorithm) +
                        ", which this server never offered");
                return false;
            }
        }

        // A batch is a run of length-prefixed packets, so the length is read where
        // the last one ended rather than through a reader that has already handed
        // the rest away.
        std::size_t at = 0;
        while (at < batch.size()) {
            std::uint32_t length = 0;
            int shift = 0;
            bool complete = false;
            while (at < batch.size() && shift <= 28) {
                const std::uint8_t part = batch[at++];
                length |= static_cast<std::uint32_t>(part & 0x7fU) << shift;
                shift += 7;
                if ((part & 0x80U) == 0) {
                    complete = true;
                    break;
                }
            }
            if (!complete || length == 0 || at + length > batch.size()) {
                problem("a batch said one of its packets was longer than the batch");
                return false;
            }

            ByteReader inner{batch.subspan(at, length)};
            at += length;
            const std::uint32_t header = readVarUInt(inner);
            const auto id = static_cast<std::uint16_t>(header & kPacketIdMask);
            ++counts_[id];
            handle(static_cast<Packet>(id), inner);
            if (!running_) {
                return true;
            }
        }
    }
    return true;
}

void BedrockBot::handle(const Packet packet, ByteReader& reader) {
    switch (packet) {
    case Packet::NetworkSettings: {
        const std::uint16_t threshold = readShortLE(reader);
        const std::uint16_t algorithm = readShortLE(reader);
        if (algorithm != 0 && algorithm != 1) {
            problem("the server named compression algorithm " + std::to_string(algorithm));
        }
        say("network settings: compress over " + std::to_string(threshold) + " bytes");
        compression_ = true;
        sendLogin();
        return;
    }
    case Packet::ServerToClientHandshake:
        onHandshake(reader);
        return;
    case Packet::PlayStatus: {
        const std::int32_t status = reader.readInt();
        if (status == 0) {
            say("the login was accepted");
        } else if (status == 3) {
            spawned_ = true;
            say("spawned");
        } else {
            refused_ = true;
            problem("the server answered the login with status " + std::to_string(status));
        }
        return;
    }
    case Packet::ResourcePacksInfo: {
        ByteWriter answer;
        writeVarUInt(answer,
                     2); // Downloading finished: there was nothing to download.
        writeString(answer, "downloadingfinished");
        send(Packet::ResourcePackClientResponse, answer.bytes());
        return;
    }
    case Packet::ResourcePackStack: {
        ByteWriter answer;
        writeVarUInt(answer, 3); // The stack is settled.
        writeString(answer, "resourcepackstackfinished");
        send(Packet::ResourcePackClientResponse, answer.bytes());
        return;
    }
    case Packet::StartGame: {
        // The two entity ids lead the packet, and the position follows the game type. A client that
        // never reads them cannot say who it is when it moves.
        static_cast<void>(readVarLong(reader));
        runtimeEntityId_ = readVarULong(reader);
        static_cast<void>(readVarInt(reader));
        ByteReader positions{reader.rest()};
        const auto readFloat = [&positions] {
            std::uint32_t raw = 0;
            for (int shift = 0; shift < 32; shift += 8) {
                raw |= static_cast<std::uint32_t>(positions.readByte()) << shift;
            }
            float value = 0.0F;
            __builtin_memcpy(&value, &raw, sizeof(value));
            return value;
        };
        x_ = readFloat();
        y_ = readFloat();
        z_ = readFloat();
        say("the world was described, spawning at " + std::to_string(x_) + ", " + std::to_string(y_) + ", " +
            std::to_string(z_));
        return;
    }
    case Packet::Text: {
        // What a client draws in its chat window: the same lines a Java player sees, if the server
        // is sending to both editions.
        static_cast<void>(reader.readByte()); // Whether it needs translating.
        const std::uint8_t shape = reader.readByte();
        static_cast<void>(reader.readByte()); // Which kind of line it is.
        if (shape == 1) {
            static_cast<void>(readString(reader));
        }
        const std::string line = readString(reader);
        if (!reader.failed() && !line.empty()) {
            say("heard: " + line);
        }
        return;
    }
    case Packet::ChunkRadiusUpdated: {
        const std::int32_t radius = readVarInt(reader);
        if (radius <= 0) {
            problem("the server allowed a draw distance of " + std::to_string(radius));
        }
        return;
    }
    case Packet::Disconnect:
        refused_ = true;
        say("the server sent us away");
        running_ = false;
        return;
    default:
        return;
    }
}

void BedrockBot::sendLogin() {
    const std::string identity =
        R"({"identityPublicKey":")" + keys_->publicKeyBase64() + R"(","extraData":{"displayName":"SwarmBot)" +
        std::to_string(options_.id) + R"(","identity":"00000000-0000-0000-0000-)" +
        std::string(12 - std::to_string(options_.id).size(), '0') + std::to_string(options_.id) +
        R"(","XUID":"25350000000000)" + std::to_string(options_.id % 100 + 10) + R"("}})";
    const auto chainJwt = keys_->signJwt(identity);
    const auto clientJwt = keys_->signJwt(R"({"DeviceOS":7,"DeviceModel":"Basalt Swarm","GameVersion":")" +
                                          std::string{kBedrockVersion} + R"("})");
    if (!chainJwt.has_value() || !clientJwt.has_value()) {
        problem("could not sign the login");
        running_ = false;
        return;
    }

    const std::string certificate = R"({"chain":[")" + *chainJwt + R"("]})";
    std::string escaped;
    for (const char character : certificate) {
        if (character == '"' || character == '\\') {
            escaped += '\\';
        }
        escaped += character;
    }
    const std::string auth = R"({"AuthenticationType":1,"Certificate":")" + escaped + R"(","Token":""})";

    ByteWriter blob;
    writeIntLE(blob, static_cast<std::int32_t>(auth.size()));
    blob.writeBytes({reinterpret_cast<const std::uint8_t*>(auth.data()), auth.size()});
    writeIntLE(blob, static_cast<std::int32_t>(clientJwt->size()));
    blob.writeBytes({reinterpret_cast<const std::uint8_t*>(clientJwt->data()), clientJwt->size()});

    ByteWriter body;
    body.writeInt(options_.protocol);
    writeVarUInt(body, static_cast<std::uint32_t>(blob.size()));
    body.writeBytes(blob.bytes());
    send(Packet::Login, body.bytes());
}

void BedrockBot::sendChat(const std::string& line) {
    ByteWriter body;
    body.writeByte(0);                            // Not a translation key.
    body.writeByte(1);                            // An author and a message, which is what a chat line is.
    body.writeByte(static_cast<std::uint8_t>(1)); // Chat.
    writeString(body, "SwarmBot" + std::to_string(options_.id));
    writeString(body, line);
    writeString(body, ""); // No xuid.
    writeString(body, ""); // No platform chat id.
    body.writeByte(0);     // Nothing filtered.
    send(Packet::Text, body.bytes());
}

void BedrockBot::sendAuthInput() {
    ByteWriter body;
    writeFloatLE(body, 0.0F); // Pitch.
    writeFloatLE(body, 0.0F); // Yaw.
    writeFloatLE(body, x_);
    writeFloatLE(body, y_);
    writeFloatLE(body, z_);
    writeFloatLE(body, 0.0F); // Where it is trying to move, sideways.
    writeFloatLE(body, 0.0F); // The same, forwards.
    writeFloatLE(body, 0.0F); // Head yaw.
    body.writeByte(0);        // No input flags.
    writeVarUInt(body, 0);    // Input mode: none.
    writeVarUInt(body, 0);    // Play mode: normal.
    writeVarInt(body, 0);     // Interaction model: touch.
    writeFloatLE(body, 0.0F); // Interact rotation.
    writeFloatLE(body, 0.0F);
    writeVarULong(body, tick_++);
    writeFloatLE(body, 0.0F); // Delta.
    writeFloatLE(body, 0.0F);
    writeFloatLE(body, 0.0F);
    for (int optional = 0; optional < 5; ++optional) {
        body.writeByte(0); // No item use, no stack request, no actions, no vehicle.
    }
    writeFloatLE(body, 0.0F); // Analog move.
    writeFloatLE(body, 0.0F);
    writeFloatLE(body, 0.0F); // Camera orientation.
    writeFloatLE(body, 0.0F);
    writeFloatLE(body, 0.0F);
    writeFloatLE(body, 0.0F); // Raw move.
    writeFloatLE(body, 0.0F);
    send(Packet::PlayerAuthInput, body.bytes());
}

void BedrockBot::onHandshake(ByteReader& reader) {
    const std::string token = readString(reader);
    const auto parts = decodeJwt(token);
    if (!parts.has_value()) {
        problem("the handshake was not a token");
        running_ = false;
        return;
    }

    const auto x5u = jsonString(parts->header, "x5u");
    const auto saltText = jsonString(parts->payload, "salt");
    if (!x5u.has_value() || !saltText.has_value()) {
        problem("the handshake carried no key or no salt");
        running_ = false;
        return;
    }
    const auto serverKey = base64Decode(*x5u);
    const auto salt = base64Decode(*saltText);
    if (!serverKey.has_value() || !salt.has_value()) {
        problem("the handshake's key or salt was not base64");
        running_ = false;
        return;
    }
    // A real client checks this before it trusts the key inside, and so does
    // this.
    if (!verifyJwt(*parts, *serverKey)) {
        problem("the handshake was not signed by the key it names");
    }

    const auto secret = keys_->sharedSecret(*serverKey);
    if (!secret.has_value()) {
        problem("the server's key is not one we can agree with");
        running_ = false;
        return;
    }
    cipher_ = std::make_unique<Cipher>(deriveSessionKey(*salt, *secret));
    say("encryption on");
    send(Packet::ClientToServerHandshake, {});
}

void BedrockBot::run() {
    auto keys = KeyPair::generate();
    if (!keys.has_value()) {
        problem("could not make a key pair");
        return;
    }
    keys_ = std::make_unique<KeyPair>(std::move(*keys));

    const auto advertisement = rak_.ping();
    if (!advertisement.has_value()) {
        problem("the server did not answer a ping");
        return;
    }
    say("server list: " + advertisement->motd + " (" + advertisement->version + ", protocol " +
        std::to_string(advertisement->protocol) + ", " + std::to_string(advertisement->online) + "/" +
        std::to_string(advertisement->max) + ")");
    if (advertisement->protocol != options_.protocol) {
        say("the server speaks protocol " + std::to_string(advertisement->protocol) + " and this bot claims " +
            std::to_string(options_.protocol));
    }

    std::string problemText;
    if (!rak_.connect(problemText)) {
        problem(problemText);
        return;
    }
    say("raknet session open, mtu " + std::to_string(rak_.mtu()));

    ByteWriter request;
    request.writeInt(options_.protocol);
    send(Packet::RequestNetworkSettings, request.bytes());

    const std::int64_t deadline = nowMillis() + 30000;
    while (running_ && nowMillis() < deadline && !refused_) {
        if (!pump(200)) {
            break;
        }
        if (spawned_) {
            break;
        }
    }

    if (!spawned_ && !refused_) {
        problem("never reached the world");
    }
    if (spawned_) {
        // A client that is told to spawn and given no columns stands in the void:
        // the chunks are what a player would notice missing, so they are what a bot
        // checks for.
        if (counts_[static_cast<std::uint16_t>(Packet::LevelChunk)] == 0) {
            problem("spawned without being sent a single chunk");
        }
        if (counts_[static_cast<std::uint16_t>(Packet::StartGame)] == 0) {
            problem("spawned without being told what world this is");
        }
        say("in the world with " + std::to_string(counts_[static_cast<std::uint16_t>(Packet::LevelChunk)]) +
            " chunk(s)");
    }
    if (spawned_) {
        // What a client sends the moment it is in: it is initialised, and then it starts telling
        // the server where it is, every tick, for as long as it is there.
        ByteWriter initialised;
        writeVarULong(initialised, runtimeEntityId_);
        send(Packet::SetLocalPlayerAsInitialized, initialised.bytes());
        sendAuthInput();
    }
    if (spawned_ && !options_.chatLine.empty()) {
        sendChat(options_.chatLine);
    }
    if (spawned_ && options_.stayMillis > 0) {
        const std::int64_t leaveAt = nowMillis() + options_.stayMillis;
        std::int64_t nextStep = nowMillis();
        while (running_ && nowMillis() < leaveAt) {
            if (!pump(100)) {
                break;
            }
            if (nowMillis() >= nextStep) {
                // A step east every tenth of a second, which is about what a walking player does.
                x_ += 0.2F;
                sendAuthInput();
                nextStep = nowMillis() + 100;
            }
        }
    }
    rak_.disconnect();
}

} // namespace basalt::swarm::bedrock
