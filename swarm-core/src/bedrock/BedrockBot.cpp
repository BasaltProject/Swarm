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
            return;
        }
        if (status == 3) {
            spawned_ = true;
            say("spawned");
            return;
        }
        refused_ = true;
        const std::string told = status == 1 ? "this client is too old" : "this server is too old";
        // Being turned away is only a problem when the server had just said, in its own server
        // list, that it speaks exactly what this bot claimed.
        if (advertisedProtocol_ == options_.protocol) {
            problem("the server advertises protocol " + std::to_string(advertisedProtocol_) +
                    " and then turned that same protocol away: " + told);
            return;
        }
        say("turned away: " + told);
        return;
    }
    case Packet::ResourcePacksInfo: {
        // Downloading finished: there was nothing to download. Below 2168 the answer is the state's
        // own number as a byte and a list of pack ids counted by a short; from 2168 it is the number
        // one lower, as a varint, with the state's name behind it.
        sendPackResponse(3, "downloadingfinished");
        return;
    }
    case Packet::ResourcePackStack: {
        sendPackResponse(4, "resourcepackstackfinished"); // The stack is settled.
        return;
    }
    case Packet::ModalFormRequest: {
        // A form is what a Bedrock client gets where a Java one gets a window of items. A bot
        // answers the first button, which is what says the whole path works: plugin, server,
        // client, and back.
        const std::uint32_t formId = readVarUInt(reader);
        const std::string form = readString(reader);
        if (reader.failed()) {
            problem("a form arrived that could not be read");
            return;
        }
        say("a form arrived: " + form);
        ByteWriter answer;
        writeVarUInt(answer, formId);
        answer.writeByte(1); // There is an answer.
        writeString(answer, "0\n");
        answer.writeByte(0); // Not dismissed.
        send(Packet::ModalFormResponse, answer.bytes());
        return;
    }
    case Packet::StartGame: {
        // Read whole, to the last byte. It is the longest packet a server writes and the only one
        // whose tail nothing else would notice: a field of the wrong width anywhere in the middle
        // leaves the rest of it off by a few bytes, and the only way to see that is to run out of
        // packet at the end, or to have some left over.
        static_cast<void>(readVarLong(reader)); // The player's own entity id, twice over.
        runtimeEntityId_ = readVarULong(reader);
        static_cast<void>(readVarInt(reader)); // What game they are playing.
        x_ = readFloatLE(reader);
        y_ = readFloatLE(reader);
        z_ = readFloatLE(reader);
        static_cast<void>(readFloatLE(reader)); // Where they are looking.
        static_cast<void>(readFloatLE(reader));

        // The world, which is most of the packet.
        static_cast<void>(readLongLE(reader));  // The seed,
        static_cast<void>(readShortLE(reader)); // which biome the spawn is in,
        static_cast<void>(readString(reader));  // and its name if it has one.
        for (int field = 0; field < 3; ++field) {
            static_cast<void>(readVarInt(reader)); // Dimension, generator, and the world's game.
        }
        static_cast<void>(reader.readByte());   // Not hardcore,
        static_cast<void>(readVarInt(reader));  // at some difficulty,
        static_cast<void>(readVarInt(reader));  // with a spawn: x,
        static_cast<void>(readVarUInt(reader)); // its height, which is the one unsigned coordinate,
        static_cast<void>(readVarInt(reader));  // and z.
        static_cast<void>(reader.readByte());   // Achievements,
        static_cast<void>(readVarInt(reader));  // what kind of editor world it is,
        static_cast<void>(reader.readByte());   // whether it was made in one,
        static_cast<void>(reader.readByte());   // whether it was exported from one,
        static_cast<void>(readVarInt(reader));  // where the day stops,
        static_cast<void>(readVarUInt(reader)); // what education offers,
        static_cast<void>(reader.readByte());   // whether they are on,
        static_cast<void>(readString(reader));  // and under what product.
        static_cast<void>(readFloatLE(reader)); // How hard it rains
        static_cast<void>(readFloatLE(reader)); // and thunders.
        for (int flag = 0; flag < 3; ++flag) {
            static_cast<void>(reader.readByte()); // Locked content, multiplayer, and the local network.
        }
        static_cast<void>(readVarInt(reader)); // Who it is broadcast to, on both counts.
        static_cast<void>(readVarInt(reader));
        static_cast<void>(reader.readByte()); // Commands,
        static_cast<void>(reader.readByte()); // and whether packs are required.
        const std::uint32_t rules = readVarUInt(reader);
        if (rules != 0) {
            problem("the server sent " + std::to_string(rules) + " gamerules, which this bot cannot read");
            return;
        }
        const std::int32_t experiments = readIntLE(reader);
        if (experiments != 0) {
            problem("the server sent " + std::to_string(experiments) + " experiments, which this bot cannot read");
            return;
        }
        for (int flag = 0; flag < 3; ++flag) {
            static_cast<void>(reader.readByte()); // Experiments once toggled, a bonus chest, a map.
        }
        // The one field in the whole packet that is a byte in 2168 and a varint below it.
        if (nativeShapes(options_.protocol)) {
            static_cast<void>(reader.readByte());
        } else {
            static_cast<void>(readVarInt(reader));
        }
        static_cast<void>(readIntLE(reader)); // How far chunks tick.
        for (int flag = 0; flag < 10; ++flag) {
            static_cast<void>(reader.readByte()); // Ten things locked, templated, or turned off.
        }
        static_cast<void>(readString(reader)); // Which Minecraft this is,
        static_cast<void>(readIntLE(reader));  // and how big the world may get,
        static_cast<void>(readIntLE(reader));
        static_cast<void>(reader.readByte());  // whether it is the nether,
        static_cast<void>(readString(reader)); // and the education link, a name
        static_cast<void>(readString(reader)); // and a uri.
        if (reader.readByte() != 0) {
            static_cast<void>(reader.readByte()); // Experimental gameplay, if it is forced at all.
        }
        static_cast<void>(reader.readByte()); // How restricted chat is,
        static_cast<void>(reader.readByte()); // and whether anybody may touch anything.
        if (options_.protocol >= 1001) {
            static_cast<void>(readVarInt(reader)); // An editor connection policy, and what it drops,
            static_cast<void>(reader.readByte());  // neither of which 975 and below know about.
        }

        static_cast<void>(readString(reader));  // The world's id,
        static_cast<void>(readString(reader));  // its name,
        static_cast<void>(readString(reader));  // the template it came from,
        static_cast<void>(reader.readByte());   // whether it is a trial,
        static_cast<void>(readVarInt(reader));  // how much movement it rewinds,
        static_cast<void>(reader.readByte());   // who breaks blocks,
        static_cast<void>(readLongLE(reader));  // what tick it is,
        static_cast<void>(readVarInt(reader));  // the enchantment seed,
        static_cast<void>(readVarUInt(reader)); // and no blocks of its own.
        static_cast<void>(readString(reader));  // What correlates this session,
        static_cast<void>(reader.readByte());   // who owns the inventories,
        static_cast<void>(readString(reader));  // what engine is running,
        skipTag(reader);                        // what properties a player has,
        static_cast<void>(readLongLE(reader));  // the checksum of the block registry,
        static_cast<void>(readUuid(reader));    // and the template's id.
        static_cast<void>(reader.readByte());   // Who generates chunks,
        static_cast<void>(reader.readByte());   // how block ids are numbered,
        static_cast<void>(reader.readByte());   // and who plays the sounds.
        if (options_.protocol >= 1001 && !nativeShapes(options_.protocol)) {
            static_cast<void>(reader.readByte()); // Whether chat is logged, which only 1001 asks.
        }
        static_cast<void>(reader.readByte()); // No join info,
        for (int field = 0; field < 4; ++field) {
            static_cast<void>(readString(reader)); // and the four names of this server and world.
        }

        if (reader.failed()) {
            problem("the world description ran off the end of the packet");
            return;
        }
        if (reader.remaining() != 0) {
            problem("the world description left " + std::to_string(reader.remaining()) + " bytes unread");
            return;
        }
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
    case Packet::SetTitle: {
        // What a client draws over the world: a title, its subtitle, or the line over the hotbar.
        const std::int32_t type = readVarInt(reader);
        const std::string text = readString(reader);
        if (!reader.failed() && !text.empty()) {
            say("a title of kind " + std::to_string(type) + ": " + text);
        }
        return;
    }
    case Packet::MovePlayer: {
        // Where the server says this client is, which for a teleport is not where it thought.
        static_cast<void>(readVarULong(reader));
        const float x = readFloatLE(reader);
        const float y = readFloatLE(reader);
        const float z = readFloatLE(reader);
        if (!reader.failed()) {
            x_ = x;
            y_ = y;
            z_ = z;
            say("moved by the server to " + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z));
        }
        return;
    }
    case Packet::PlayerList: {
        // Who else is here. Read whole, skin and all: the fields after a skin only land where they
        // should if every one of its own did, so reaching the end of the packet is the test.
        // Below 2168 the action is said once and in front, and what follows is all of that kind.
        const bool wholePacketArriving = nativeShapes(options_.protocol) || reader.readByte() == 0;
        const std::uint32_t entries = readVarUInt(reader);
        std::uint32_t added = 0;
        for (std::uint32_t index = 0; index < entries && !reader.failed(); ++index) {
            bool arriving = wholePacketArriving;
            if (nativeShapes(options_.protocol)) {
                arriving = readVarUInt(reader) == 1;
                const std::uint8_t action = reader.readByte();
                if (arriving != (action == 0)) {
                    problem("a player list line says it is arriving and leaving at once");
                }
            }
            const std::string uuid = readUuid(reader);
            if (!arriving) {
                listed_.erase(uuid);
                say("told that " + uuid + " is gone");
                continue;
            }
            static_cast<void>(readVarLong(reader)); // Their entity id,
            const std::string name = readString(reader);
            static_cast<void>(readString(reader)); // their xuid,
            static_cast<void>(readString(reader)); // and what console they chat from.
            static_cast<void>(readIntLE(reader));
            skipSkin(reader, options_.protocol);
            static_cast<void>(reader.readByte()); // A teacher,
            static_cast<void>(reader.readByte()); // the host,
            static_cast<void>(reader.readByte()); // or somebody's second player on one console.
            if (nativeShapes(options_.protocol)) {
                static_cast<void>(readIntLE(reader));
            }
            if (reader.failed()) {
                problem("a player list line ran off the end of the packet");
                break;
            }
            ++added;
            listed_[uuid] = name;
            say("told about " + name + " in the player list");
        }
        // Below 2168 whether a skin is to be trusted is not part of the line: it is a run of
        // booleans behind all of them, one each.
        if (!nativeShapes(options_.protocol)) {
            for (std::uint32_t index = 0; index < added && !reader.failed(); ++index) {
                static_cast<void>(reader.readByte());
            }
        }
        if (!reader.failed() && reader.remaining() != 0) {
            problem("a player list left " + std::to_string(reader.remaining()) + " bytes unread");
        }
        return;
    }
    case Packet::AddPlayer: {
        const std::string uuid = readUuid(reader);
        const std::string name = readString(reader);
        const std::uint64_t runtime = readVarULong(reader);
        static_cast<void>(readString(reader)); // What console they chat from.
        const float x = readFloatLE(reader);
        const float y = readFloatLE(reader);
        const float z = readFloatLE(reader);
        if (reader.failed()) {
            problem("a player was added with a body that ran out");
            return;
        }
        if (runtime == runtimeEntityId_) {
            problem("the server added " + name + " with this bot's own entity id");
        }
        if (!listed_.contains(uuid)) {
            problem("the server drew " + name + " without listing them first");
        }
        seen_[runtime] = name;
        say("can see " + name + " at " + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z));
        return;
    }
    case Packet::MoveEntityAbsolute: {
        const std::uint64_t runtime = readVarULong(reader);
        static_cast<void>(reader.readByte()); // Whether they are on the ground, teleported or forced.
        const float x = readFloatLE(reader);
        const float y = readFloatLE(reader);
        const float z = readFloatLE(reader);
        static_cast<void>(reader.readByte()); // The three turns, a byte each.
        static_cast<void>(reader.readByte());
        static_cast<void>(reader.readByte());
        if (reader.failed()) {
            return;
        }
        if (!seen_.contains(runtime)) {
            problem("the server moved entity " + std::to_string(runtime) + ", which it never drew");
            return;
        }
        say(seen_[runtime] + " moved to " + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z));
        return;
    }
    case Packet::RemoveEntity: {
        const std::int64_t entityId = readVarLong(reader);
        if (!reader.failed()) {
            seen_.erase(static_cast<std::uint64_t>(entityId));
            say("entity " + std::to_string(entityId) + " is gone");
        }
        return;
    }
    case Packet::AvailableCommands: {
        // What this client is allowed to type. Read whole: a Bedrock client parses a line against
        // this list before it sends anything, so a name missing here is a command the player cannot
        // run at all, and a field of the wrong width loses every name after it.
        for (int list = 0; list < 5; ++list) {
            // Enum values, subcommand words, suffixes, enums and subcommands: a server with none of
            // them writes five empty lists, and a bot that meets one cannot read what follows.
            const std::uint32_t count = readVarUInt(reader);
            if (count != 0) {
                problem("the command list carries " + std::to_string(count) + " of list " + std::to_string(list) +
                        ", which this bot cannot read");
                return;
            }
        }
        const std::uint32_t count = readVarUInt(reader);
        for (std::uint32_t index = 0; index < count && !reader.failed(); ++index) {
            const std::string name = readString(reader);
            static_cast<void>(readString(reader));  // What its help line says,
            static_cast<void>(readShortLE(reader)); // its flags,
            static_cast<void>(readString(reader));  // who may run it,
            static_cast<void>(readIntLE(reader));   // which enum its aliases are,
            const std::uint32_t subcommands = readVarUInt(reader);
            for (std::uint32_t at = 0; at < subcommands && !reader.failed(); ++at) {
                static_cast<void>(readIntLE(reader));
            }
            const std::uint32_t overloads = readVarUInt(reader);
            for (std::uint32_t at = 0; at < overloads && !reader.failed(); ++at) {
                static_cast<void>(reader.readByte()); // Whether it chains,
                const std::uint32_t parameters = readVarUInt(reader);
                for (std::uint32_t parameter = 0; parameter < parameters && !reader.failed(); ++parameter) {
                    static_cast<void>(readString(reader)); // what the parameter is called,
                    static_cast<void>(readIntLE(reader));  // what it is,
                    static_cast<void>(reader.readByte());  // whether it may be left out,
                    static_cast<void>(reader.readByte());  // and what else it carries.
                }
            }
            if (!reader.failed()) {
                offered_.insert(name);
            }
        }
        static_cast<void>(readVarUInt(reader)); // No soft enums
        static_cast<void>(readVarUInt(reader)); // and nothing constraining one.
        if (reader.failed()) {
            problem("the command list ran off the end of the packet");
            return;
        }
        if (reader.remaining() != 0) {
            problem("the command list left " + std::to_string(reader.remaining()) + " bytes unread");
            return;
        }
        std::string named;
        for (const std::string& command : offered_) {
            named += (named.empty() ? "" : ", ") + command;
        }
        say("offered " + std::to_string(offered_.size()) + " command(s): " + named);
        return;
    }
    case Packet::LevelEvent: {
        // Something that happens to the world rather than to anybody. Only the weather is read: the
        // rest are effects a bot has nothing to draw with.
        const std::int32_t type = readVarInt(reader);
        static_cast<void>(readFloatLE(reader));
        static_cast<void>(readFloatLE(reader));
        static_cast<void>(readFloatLE(reader));
        const std::int32_t data = readVarInt(reader);
        if (reader.failed()) {
            return;
        }
        switch (type) {
        case 3001:
            say("it is raining, at " + std::to_string(data));
            break;
        case 3002:
            say("it is thundering, at " + std::to_string(data));
            break;
        case 3003:
            say("it stopped raining");
            break;
        case 3004:
            say("it stopped thundering");
            break;
        default:
            break;
        }
        return;
    }
    case Packet::UpdateAttributes: {
        // What the HUD draws from. Bedrock is told the range of every number as well as the number,
        // so a value outside its own range is a server bug the client would draw wrong.
        const std::uint64_t runtime = readVarULong(reader);
        const std::uint32_t count = readVarUInt(reader);
        for (std::uint32_t index = 0; index < count && !reader.failed(); ++index) {
            const float minimum = readFloatLE(reader);
            const float maximum = readFloatLE(reader);
            const float value = readFloatLE(reader);
            static_cast<void>(readFloatLE(reader)); // The range it started with,
            static_cast<void>(readFloatLE(reader));
            static_cast<void>(readFloatLE(reader)); // and what it was to begin with.
            const std::string name = readString(reader);
            const std::uint32_t modifiers = readVarUInt(reader);
            for (std::uint32_t at = 0; at < modifiers && !reader.failed(); ++at) {
                static_cast<void>(readString(reader));
                static_cast<void>(readString(reader));
                static_cast<void>(readFloatLE(reader));
                static_cast<void>(readIntLE(reader));
                static_cast<void>(readIntLE(reader));
                static_cast<void>(reader.readByte());
            }
            if (reader.failed()) {
                problem("an attribute ran off the end of the packet");
                break;
            }
            if (value < minimum || value > maximum) {
                problem(name + " is " + std::to_string(value) + ", outside its own range");
            }
            if (runtime == runtimeEntityId_) {
                attributes_[name] = value;
            }
            say(name + " is " + std::to_string(value));
        }
        if (!reader.failed()) {
            static_cast<void>(readVarULong(reader)); // Which tick it is.
        }
        return;
    }
    case Packet::SetPlayerGameType: {
        const std::int32_t mode = readVarInt(reader);
        if (!reader.failed()) {
            say("the game mode is now " + std::to_string(mode));
        }
        return;
    }
    case Packet::SetTime: {
        const std::int32_t time = readVarInt(reader);
        if (!reader.failed()) {
            say("the world says the time is " + std::to_string(time));
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

void BedrockBot::sendPackResponse(const std::uint8_t status, const std::string& name) {
    ByteWriter answer;
    if (nativeShapes(options_.protocol)) {
        writeVarUInt(answer, static_cast<std::uint32_t>(status) - 1);
        writeString(answer, name);
    } else {
        answer.writeByte(status);
        writeShortLE(answer, 0); // No pack ids to name.
    }
    send(Packet::ResourcePackClientResponse, answer.bytes());
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

void BedrockBot::sendCommand(const std::string& line) {
    ByteWriter body;
    writeString(body, "/" + line);
    // Where the client says the command came from: its own name for the kind of console, who is
    // asking, the request it belongs to, and which entity that is.
    writeString(body, "player");
    for (int index = 0; index < 16; ++index) {
        body.writeByte(0);
    }
    writeString(body, "");
    for (int index = 0; index < 8; ++index) {
        body.writeByte(0);
    }
    body.writeByte(0);     // Not the server talking to itself.
    writeString(body, ""); // The version of the command syntax, which a server reads from its own tree.
    send(Packet::CommandRequest, body.bytes());
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
    advertisedProtocol_ = advertisement->protocol;
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
    if (refused_ && !spawned_) {
        rak_.disconnect();
        return;
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
    if (spawned_ && !options_.commandLine.empty()) {
        sendCommand(options_.commandLine);
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
