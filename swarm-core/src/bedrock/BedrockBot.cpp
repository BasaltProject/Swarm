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
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

/// One item on the wire. Air is only its id: a zero there ends the item, and
/// everything after it belongs to whatever comes next in the packet. What a
/// stack says about itself, for a bot that wants to print it.
struct Stack {
  std::int32_t id = 0;
  std::int32_t count = 0;
  std::int32_t data = 0;
};

[[nodiscard]] Stack readWindowItem(ByteReader &reader, int protocol);

/// One stack in a hand. From 2168 it is the same shape a window carries.
[[nodiscard]] Stack readItem(ByteReader &reader, const int protocol) {
  if (protocol >= 2168) {
    return readWindowItem(reader, protocol);
  }
  const std::int32_t id = readVarInt(reader);
  if (id == 0) {
    return {};
  }
  const auto count = static_cast<std::int32_t>(readShortLE(reader));
  const auto data = static_cast<std::int32_t>(readVarUInt(reader));
  if (reader.readByte() != 0) {
    static_cast<void>(readVarInt(
        reader)); // and which one of its kind, when the server counts them.
  }
  static_cast<void>(readVarInt(reader)); // Which block it places.
  const std::uint32_t extra = readVarUInt(reader);
  for (std::uint32_t at = 0; at < extra && !reader.failed(); ++at) {
    static_cast<void>(reader.readByte()); // The nbt and the lists, which this
                                          // bot does not read.
  }
  return {id, count, data};
}

/// One stack as a window carries it. Below 1001 it is the shape an entity's
/// hand used to use; from 1001 the id is a short and an empty stack still says
/// all of itself, and 2168 dropped the number that said which kind of id
/// followed.
[[nodiscard]] Stack readWindowItem(ByteReader &reader, const int protocol) {
  if (protocol < 1001) {
    return readItem(reader, protocol);
  }
  const auto id =
      static_cast<std::int32_t>(static_cast<std::int16_t>(readShortLE(reader)));
  const auto count = static_cast<std::int32_t>(readShortLE(reader));
  const auto data = static_cast<std::int32_t>(readVarUInt(reader));
  if (reader.readByte() != 0) {
    if (protocol < 2168) {
      static_cast<void>(
          readVarUInt(reader)); // which of the three kinds of id it is,
    }
    static_cast<void>(readVarInt(reader)); // and which one of its kind.
  }
  static_cast<void>(readVarUInt(reader)); // Which block it places.
  const std::uint32_t extra = readVarUInt(reader);
  for (std::uint32_t at = 0; at < extra && !reader.failed(); ++at) {
    static_cast<void>(reader.readByte());
  }
  return {id, count, data};
}

/// A point written as three numbers with commas between them.
[[nodiscard]] std::array<std::int32_t, 3> pointOf(const std::string &text) {
  std::array<std::int32_t, 3> point{0, 0, 0};
  std::size_t at = 0;
  for (std::size_t part = 0; part < 3 && at <= text.size(); ++part) {
    const std::size_t comma = std::min(text.find(',', at), text.size());
    point[part] = static_cast<std::int32_t>(
        std::strtol(text.substr(at, comma - at).c_str(), nullptr, 10));
    at = comma + 1;
  }
  return point;
}

/// One end of a move, as a stack request names it: which window, which slot of
/// it, and what the client believes is there.
void writeStackSlot(ByteWriter &writer, const std::int32_t container,
                    const std::int32_t slot, const int protocol) {
  writer.writeByte(static_cast<std::uint8_t>(container));
  writer.writeByte(0); // No id of its own: this is not a bundle.
  writer.writeByte(static_cast<std::uint8_t>(slot));
  if (protocol >= 2168) {
    writeIntLE(writer, 0);
  } else {
    writeVarInt(writer, 0);
  }
}

/// One stack where it is named rather than moved: the creative menu and an
/// entity's hand.
void readItemInstance(ByteReader &reader) {
  if (readVarInt(reader) == 0) {
    return;
  }
  static_cast<void>(readShortLE(reader)); // How many,
  static_cast<void>(readVarUInt(reader)); // how damaged,
  static_cast<void>(readVarInt(reader));  // and which block it places.
  const std::uint32_t extra = readVarUInt(reader);
  for (std::uint32_t at = 0; at < extra && !reader.failed(); ++at) {
    static_cast<void>(reader.readByte());
  }
}

/// A run of metadata fields, which four packets carry and all four carry the
/// same way. Answers the air supply when it is in there, and -1 when it is not.
[[nodiscard]] std::int16_t readMetadata(ByteReader &reader,
                                        const int protocol) {
  const std::uint32_t fields = readVarUInt(reader);
  std::int16_t air = -1;
  for (std::uint32_t at = 0; at < fields && !reader.failed(); ++at) {
    const std::uint32_t id = readVarUInt(reader);
    const std::uint32_t format = readVarUInt(reader);
    if (nativeShapes(protocol) &&
        static_cast<std::uint32_t>(reader.readByte()) != format) {
      // Since 2168 the format goes out twice, and a client refuses the two
      // disagreeing.
      return -2;
    }
    switch (format) {
    case 0:
      static_cast<void>(reader.readByte());
      break;
    case 1: {
      const auto value = static_cast<std::int16_t>(readShortLE(reader));
      if (id == 7) {
        air = value;
      }
      break;
    }
    case 2:
      static_cast<void>(readVarInt(reader));
      break;
    case 3:
      static_cast<void>(readFloatLE(reader));
      break;
    case 4:
      static_cast<void>(readString(reader));
      break;
    case 7:
      static_cast<void>(readVarLong(reader));
      break;
    default:
      return -3;
    }
  }
  return air;
}

} // namespace

BedrockBot::BedrockBot(Options options)
    : options_(std::move(options)), rak_(options_.host, options_.port) {}

BedrockBot::~BedrockBot() = default;

void BedrockBot::say(const std::string &line) const {
  if (options_.say) {
    options_.say("[bedrock " + std::to_string(options_.id) + "] " + line);
  }
}

void BedrockBot::problem(const std::string &line) {
  ++problems_;
  say("PROBLEM: " + line);
}

void BedrockBot::send(const Packet packet,
                      const std::span<const std::uint8_t> body) {
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
  for (const std::vector<std::uint8_t> &raw : payloads) {
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
        problem("a batch arrived compressed with algorithm " +
                std::to_string(algorithm) +
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

void BedrockBot::handle(const Packet packet, ByteReader &reader) {
  switch (packet) {
  case Packet::NetworkSettings: {
    const std::uint16_t threshold = readShortLE(reader);
    const std::uint16_t algorithm = readShortLE(reader);
    if (algorithm != 0 && algorithm != 1) {
      problem("the server named compression algorithm " +
              std::to_string(algorithm));
    }
    say("network settings: compress over " + std::to_string(threshold) +
        " bytes");
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
    const std::string told =
        status == 1 ? "this client is too old" : "this server is too old";
    // Being turned away is only a problem when the server had just said, in its
    // own server list, that it speaks exactly what this bot claimed.
    if (advertisedProtocol_ == options_.protocol) {
      problem("the server advertises protocol " +
              std::to_string(advertisedProtocol_) +
              " and then turned that same protocol away: " + told);
      return;
    }
    say("turned away: " + told);
    return;
  }
  case Packet::ResourcePacksInfo: {
    // Downloading finished: there was nothing to download. Below 2168 the
    // answer is the state's own number as a byte and a list of pack ids counted
    // by a short; from 2168 it is the number one lower, as a varint, with the
    // state's name behind it.
    sendPackResponse(3, "downloadingfinished");
    return;
  }
  case Packet::ResourcePackStack: {
    sendPackResponse(4, "resourcepackstackfinished"); // The stack is settled.
    return;
  }
  case Packet::ModalFormRequest: {
    // A form is what a Bedrock client gets where a Java one gets a window of
    // items. A bot answers the first button, which is what says the whole path
    // works: plugin, server, client, and back.
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
    // Read whole, to the last byte. It is the longest packet a server writes
    // and the only one whose tail nothing else would notice: a field of the
    // wrong width anywhere in the middle leaves the rest of it off by a few
    // bytes, and the only way to see that is to run out of packet at the end,
    // or to have some left over.
    static_cast<void>(
        readVarLong(reader)); // The player's own entity id, twice over.
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
      static_cast<void>(
          readVarInt(reader)); // Dimension, generator, and the world's game.
    }
    static_cast<void>(reader.readByte());  // Not hardcore,
    static_cast<void>(readVarInt(reader)); // at some difficulty,
    static_cast<void>(readVarInt(reader)); // with a spawn: x,
    static_cast<void>(readVarUInt(
        reader)); // its height, which is the one unsigned coordinate,
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
      static_cast<void>(reader.readByte()); // Locked content, multiplayer, and
                                            // the local network.
    }
    static_cast<void>(
        readVarInt(reader)); // Who it is broadcast to, on both counts.
    static_cast<void>(readVarInt(reader));
    static_cast<void>(reader.readByte()); // Commands,
    static_cast<void>(reader.readByte()); // and whether packs are required.
    const std::uint32_t rules = readVarUInt(reader);
    if (rules != 0) {
      problem("the server sent " + std::to_string(rules) +
              " gamerules, which this bot cannot read");
      return;
    }
    const std::int32_t experiments = readIntLE(reader);
    if (experiments != 0) {
      problem("the server sent " + std::to_string(experiments) +
              " experiments, which this bot cannot read");
      return;
    }
    for (int flag = 0; flag < 3; ++flag) {
      static_cast<void>(
          reader.readByte()); // Experiments once toggled, a bonus chest, a map.
    }
    // The one field in the whole packet that is a byte in 2168 and a varint
    // below it.
    if (nativeShapes(options_.protocol)) {
      static_cast<void>(reader.readByte());
    } else {
      static_cast<void>(readVarInt(reader));
    }
    static_cast<void>(readIntLE(reader)); // How far chunks tick.
    for (int flag = 0; flag < 10; ++flag) {
      static_cast<void>(
          reader.readByte()); // Ten things locked, templated, or turned off.
    }
    static_cast<void>(readString(reader)); // Which Minecraft this is,
    static_cast<void>(readIntLE(reader));  // and how big the world may get,
    static_cast<void>(readIntLE(reader));
    static_cast<void>(reader.readByte());  // whether it is the nether,
    static_cast<void>(readString(reader)); // and the education link, a name
    static_cast<void>(readString(reader)); // and a uri.
    if (reader.readByte() != 0) {
      static_cast<void>(
          reader.readByte()); // Experimental gameplay, if it is forced at all.
    }
    static_cast<void>(reader.readByte()); // How restricted chat is,
    static_cast<void>(
        reader.readByte()); // and whether anybody may touch anything.
    if (options_.protocol >= 1001) {
      static_cast<void>(readVarInt(
          reader)); // An editor connection policy, and what it drops,
      static_cast<void>(
          reader.readByte()); // neither of which 975 and below know about.
    }

    static_cast<void>(readString(reader)); // The world's id,
    static_cast<void>(readString(reader)); // its name,
    static_cast<void>(readString(reader)); // the template it came from,
    static_cast<void>(reader.readByte());  // whether it is a trial,
    // How movement is settled, in two fields. There was a third in front of
    // them naming the mode and 818 took it away: reading it anyway is how this
    // bot agreed with a server that wrote one varint too many, and neither of
    // them noticed.
    static_cast<void>(readVarInt(reader));  // how much of it it rewinds,
    static_cast<void>(reader.readByte());   // who breaks blocks,
    static_cast<void>(readLongLE(reader));  // what tick it is,
    static_cast<void>(readVarInt(reader));  // the enchantment seed,
    static_cast<void>(readVarUInt(reader)); // and no blocks of its own.
    static_cast<void>(readString(reader));  // What correlates this session,
    static_cast<void>(reader.readByte());   // who owns the inventories,
    static_cast<void>(readString(reader));  // what engine is running,
    skipTag(reader);                        // what properties a player has,
    static_cast<void>(
        readLongLE(reader));              // the checksum of the block registry,
    static_cast<void>(readUuid(reader));  // and the template's id.
    static_cast<void>(reader.readByte()); // Who generates chunks,
    static_cast<void>(reader.readByte()); // how block ids are numbered,
    static_cast<void>(reader.readByte()); // and who plays the sounds.
    if (options_.protocol >= 1001 && !nativeShapes(options_.protocol)) {
      static_cast<void>(
          reader.readByte()); // Whether chat is logged, which only 1001 asks.
    }
    static_cast<void>(reader.readByte()); // No join info,
    for (int field = 0; field < 4; ++field) {
      static_cast<void>(
          readString(reader)); // and the four names of this server and world.
    }

    if (reader.failed()) {
      problem("the world description ran off the end of the packet");
      return;
    }
    if (reader.remaining() != 0) {
      problem("the world description left " +
              std::to_string(reader.remaining()) + " bytes unread");
      return;
    }
    say("the world was described, spawning at " + std::to_string(x_) + ", " +
        std::to_string(y_) + ", " + std::to_string(z_));
    return;
  }
  case Packet::Text: {
    // What a client draws in its chat window: the same lines a Java player
    // sees, if the server is sending to both editions.
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
      problem("the server allowed a draw distance of " +
              std::to_string(radius));
    }
    return;
  }
  case Packet::SetTitle: {
    // What a client draws over the world: a title, its subtitle, or the line
    // over the hotbar.
    const std::int32_t type = readVarInt(reader);
    const std::string text = readString(reader);
    if (!reader.failed() && !text.empty()) {
      say("a title of kind " + std::to_string(type) + ": " + text);
    }
    return;
  }
  case Packet::MovePlayer: {
    // Where the server says this client is, which for a teleport is not where
    // it thought.
    static_cast<void>(readVarULong(reader));
    const float x = readFloatLE(reader);
    const float y = readFloatLE(reader);
    const float z = readFloatLE(reader);
    if (!reader.failed()) {
      x_ = x;
      y_ = y;
      z_ = z;
      say("moved by the server to " + std::to_string(x) + ", " +
          std::to_string(y) + ", " + std::to_string(z));
    }
    return;
  }
  case Packet::PlayerList: {
    // Who else is here. Read whole, skin and all: the fields after a skin only
    // land where they should if every one of its own did, so reaching the end
    // of the packet is the test. Below 2168 the action is said once and in
    // front, and what follows is all of that kind.
    const bool wholePacketArriving =
        nativeShapes(options_.protocol) || reader.readByte() == 0;
    const std::uint32_t entries = readVarUInt(reader);
    std::uint32_t added = 0;
    for (std::uint32_t index = 0; index < entries && !reader.failed();
         ++index) {
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
      static_cast<void>(
          reader.readByte()); // or somebody's second player on one console.
      static_cast<void>(
          readIntLE(reader)); // What colour their name is drawn in.
      if (reader.failed()) {
        problem("a player list line ran off the end of the packet");
        break;
      }
      ++added;
      listed_[uuid] = name;
      say("told about " + name + " in the player list");
    }
    // Below 2168 whether a skin is to be trusted is not part of the line: it is
    // a run of booleans behind all of them, one each.
    if (!nativeShapes(options_.protocol)) {
      for (std::uint32_t index = 0; index < added && !reader.failed();
           ++index) {
        static_cast<void>(reader.readByte());
      }
    }
    if (!reader.failed() && reader.remaining() != 0) {
      problem("a player list left " + std::to_string(reader.remaining()) +
              " bytes unread");
    }
    return;
  }
  case Packet::AddEntity: {
    static_cast<void>(readVarLong(reader));
    const std::uint64_t runtime = readVarULong(reader);
    const std::string identifier = readString(reader);
    const float x = readFloatLE(reader);
    const float y = readFloatLE(reader);
    const float z = readFloatLE(reader);
    if (reader.failed()) {
      problem("an entity was added with a body that ran out");
      return;
    }
    seen_[runtime] = identifier;
    say("can see " + identifier + " at " + std::to_string(x) + ", " +
        std::to_string(y) + ", " + std::to_string(z));
    return;
  }
  case Packet::AddItemEntity: {
    static_cast<void>(readVarLong(reader));
    const std::uint64_t runtime = readVarULong(reader);
    const Stack item = readItem(reader, options_.protocol);
    const float x = readFloatLE(reader);
    const float y = readFloatLE(reader);
    const float z = readFloatLE(reader);
    if (reader.failed()) {
      problem("an item was added with a body that ran out");
      return;
    }
    seen_[runtime] = "an item";
    say("can see a dropped item (" + std::to_string(item.count) + " of " +
        std::to_string(item.id) + ") at " + std::to_string(x) + ", " +
        std::to_string(y) + ", " + std::to_string(z));
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
    static_cast<void>(readFloatLE(reader)); // How fast they are going,
    static_cast<void>(readFloatLE(reader));
    static_cast<void>(readFloatLE(reader));
    static_cast<void>(
        readFloatLE(reader)); // and which way they are looking, in three.
    static_cast<void>(readFloatLE(reader));
    static_cast<void>(readFloatLE(reader));
    static_cast<void>(readItem(reader, options_.protocol));
    static_cast<void>(readVarInt(reader)); // Which game type they are on.
    static_cast<void>(readMetadata(reader, options_.protocol));
    static_cast<void>(
        readVarUInt(reader)); // Entity properties, of which a player has none.
    static_cast<void>(readVarUInt(reader));
    static_cast<void>(readLongLE(reader)); // The abilities: whose they are,
    static_cast<void>(reader.readByte());  // what they may do here,
    static_cast<void>(reader.readByte());  // what commands they may run,
    const std::uint32_t layers = readVarUInt(reader);
    for (std::uint32_t at = 0; at < layers && !reader.failed(); ++at) {
      static_cast<void>(
          readIntLE(reader)); // and a layer each of which is five numbers.
      static_cast<void>(readIntLE(reader));
      static_cast<void>(readIntLE(reader));
      static_cast<void>(readIntLE(reader));
      static_cast<void>(readFloatLE(reader));
    }
    static_cast<void>(
        readVarUInt(reader));              // What they are riding or ridden by.
    static_cast<void>(readString(reader)); // Which device,
    static_cast<void>(readIntLE(reader));  // of which platform.
    if (reader.failed()) {
      problem("a player was added with a body that ran out");
      return;
    }
    if (reader.remaining() != 0) {
      problem("a player was added and left " +
              std::to_string(reader.remaining()) + " bytes unread");
      return;
    }
    if (runtime == runtimeEntityId_) {
      problem("the server added " + name + " with this bot's own entity id");
    }
    if (!listed_.contains(uuid)) {
      problem("the server drew " + name + " without listing them first");
    }
    seen_[runtime] = name;
    say("can see " + name + " at " + std::to_string(x) + ", " +
        std::to_string(y) + ", " + std::to_string(z));
    return;
  }
  case Packet::MoveEntityAbsolute: {
    const std::uint64_t runtime = readVarULong(reader);
    static_cast<void>(reader.readByte()); // Whether they are on the ground,
                                          // teleported or forced.
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
      problem("the server moved entity " + std::to_string(runtime) +
              ", which it never drew");
      return;
    }
    say(seen_[runtime] + " moved to " + std::to_string(x) + ", " +
        std::to_string(y) + ", " + std::to_string(z));
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
  case Packet::PlaySound: {
    // A sound, under the name Bedrock knows it by. The place travels as eighths
    // of a block.
    const std::string name = readString(reader);
    const std::int32_t x = readVarInt(reader);
    static_cast<void>(readVarUInt(reader));
    static_cast<void>(readVarInt(reader));
    static_cast<void>(readFloatLE(reader)); // How loud,
    static_cast<void>(readFloatLE(reader)); // and how high.
    if (nativeShapes(options_.protocol)) {
      static_cast<void>(readVarUInt(reader)); // How many times it loops.
    }
    if (options_.protocol >= 975) {
      static_cast<void>(
          reader.readByte()); // Whether the server named a sound of its own.
    }
    if (reader.failed()) {
      problem("a sound arrived with a body that ran out");
      return;
    }
    if (reader.remaining() != 0) {
      problem("a sound left " + std::to_string(reader.remaining()) +
              " bytes unread");
      return;
    }
    ++sounds_;
    say("heard the sound " + name + " around " + std::to_string(x / 8));
    return;
  }
  case Packet::MobEffect: {
    // An effect starting, changing or ending. Read to the last byte: this
    // packet grew twice in the window this bot speaks and both times at the
    // end.
    static_cast<void>(readVarULong(reader)); // Whose it is.
    const std::uint8_t event = reader.readByte();
    const std::int32_t id = readVarInt(reader);
    const std::int32_t amplifier = readVarInt(reader);
    static_cast<void>(reader.readByte()); // Whether it draws its own swirls.
    const std::int32_t ticks = readVarInt(reader);
    static_cast<void>(readVarULong(reader)); // Which tick it started on.
    static_cast<void>(
        reader
            .readByte()); // Whether it came from a beacon rather than a bottle.
    if (reader.failed()) {
      problem("an effect arrived with a body that ran out");
      return;
    }
    if (reader.remaining() != 0) {
      problem("an effect left " + std::to_string(reader.remaining()) +
              " bytes unread");
      return;
    }
    ++effects_;
    say("effect " + std::to_string(id) + " level " +
        std::to_string(amplifier + 1) + " for " + std::to_string(ticks) +
        " tick(s), event " + std::to_string(static_cast<std::int32_t>(event)));
    return;
  }
  case Packet::ItemComponent: {
    // Which item is which number. Read whole: everything a window says
    // afterwards is read against this list, so a line misread here is every
    // stack misread later.
    const std::uint32_t names = readVarUInt(reader);
    for (std::uint32_t at = 0; at < names && !reader.failed(); ++at) {
      static_cast<void>(readString(reader));  // Its name,
      static_cast<void>(readShortLE(reader)); // the number it answers to,
      static_cast<void>(reader.readByte());   // whether components drive it,
      static_cast<void>(readVarInt(reader));  // which version of an item it is,
      // The components, which for everything this server sends is an empty
      // compound: a tag byte, a name of nothing and the end of it.
      if (reader.readByte() != 0x0a || readVarUInt(reader) != 0 ||
          reader.readByte() != 0x00) {
        problem(
            "an item arrived with components, which this server does not send");
        return;
      }
    }
    if (reader.failed()) {
      problem("the item list ran off the end of the packet");
      return;
    }
    if (reader.remaining() != 0) {
      problem("the item list left " + std::to_string(reader.remaining()) +
              " bytes unread");
      return;
    }
    itemNames_ = static_cast<std::int32_t>(names);
    say("told about " + std::to_string(names) + " item name(s)");
    return;
  }
  case Packet::CreativeContent: {
    // What a player in creative may take. Read whole: the menu is two arrays
    // and the second is as long as the item list, so a line misread is every
    // line after it misread.
    const std::uint32_t groups = readVarUInt(reader);
    for (std::uint32_t at = 0; at < groups && !reader.failed(); ++at) {
      static_cast<void>(reader.readByte());  // Which tab it sits under,
      static_cast<void>(readString(reader)); // what it is called,
      readItemInstance(reader);              // and what is drawn on it.
    }
    const std::uint32_t offered = readVarUInt(reader);
    for (std::uint32_t at = 0; at < offered && !reader.failed(); ++at) {
      static_cast<void>(readVarUInt(reader)); // Which stack this is,
      readItemInstance(reader);               // what it is,
      static_cast<void>(readVarUInt(reader)); // and which group it is in.
    }
    if (reader.failed()) {
      problem("the creative menu ran off the end of the packet");
      return;
    }
    if (reader.remaining() != 0) {
      problem("the creative menu left " + std::to_string(reader.remaining()) +
              " bytes unread");
      return;
    }
    say("creative menu: " + std::to_string(offered) + " item(s) in " +
        std::to_string(groups) + " group(s)");
    return;
  }
  case Packet::BlockEvent: {
    // What a block asked to be done with it: a lid, a note, a piston.
    const std::int32_t x = readVarInt(reader);
    const std::int32_t y = static_cast<std::int32_t>(readVarUInt(reader));
    const std::int32_t z = readVarInt(reader);
    const std::int32_t b0 = readVarInt(reader);
    const std::int32_t b1 = readVarInt(reader);
    if (reader.failed()) {
      problem("a block event ran out");
      return;
    }
    if (reader.remaining() != 0) {
      problem("a block event left " + std::to_string(reader.remaining()) +
              " bytes unread");
      return;
    }
    say("the block at " + std::to_string(x) + ", " + std::to_string(y) + ", " +
        std::to_string(z) + " says " + std::to_string(b0) + " and " +
        std::to_string(b1));
    return;
  }
  case Packet::ContainerSetData: {
    // One of the numbers a window draws its own machinery from: a furnace's
    // fire and arrow.
    const std::int32_t containerId =
        static_cast<std::int8_t>(reader.readByte());
    const std::int32_t property = readVarInt(reader);
    const std::int32_t value = readVarInt(reader);
    if (reader.failed()) {
      problem("a window's number ran out");
      return;
    }
    if (reader.remaining() != 0) {
      problem("a window's number left " + std::to_string(reader.remaining()) +
              " bytes unread");
      return;
    }
    say("window " + std::to_string(containerId) + " says its " +
        std::to_string(property) + " is " + std::to_string(value));
    return;
  }
  case Packet::ContainerOpen: {
    const std::int32_t containerId =
        static_cast<std::int8_t>(reader.readByte());
    const std::int32_t type = static_cast<std::int8_t>(reader.readByte());
    const std::int32_t x = readVarInt(reader);
    const std::int32_t y = static_cast<std::int32_t>(readVarUInt(reader));
    const std::int32_t z = readVarInt(reader);
    static_cast<void>(readVarLong(
        reader)); // Whose window it is, when it belongs to an entity.
    if (reader.failed()) {
      problem("a window opened with a body that ran out");
      return;
    }
    if (reader.remaining() != 0) {
      problem("a window opened and left " + std::to_string(reader.remaining()) +
              " bytes unread");
      return;
    }
    say("window " + std::to_string(containerId) + " of kind " +
        std::to_string(type) + " opened at " + std::to_string(x) + ", " +
        std::to_string(y) + ", " + std::to_string(z));
    openContainer_ = containerId;
    return;
  }
  case Packet::ContainerClose: {
    const std::int32_t containerId =
        static_cast<std::int8_t>(reader.readByte());
    static_cast<void>(reader.readByte());         // Which kind it was,
    const bool byServer = reader.readByte() != 0; // and who closed it.
    if (reader.failed()) {
      problem("a window closed with a body that ran out");
      return;
    }
    if (reader.remaining() != 0) {
      problem("a window closed and left " + std::to_string(reader.remaining()) +
              " bytes unread");
      return;
    }
    say("window " + std::to_string(containerId) + " closed" +
        (byServer ? " by the server" : ""));
    openContainer_ = -1;
    return;
  }
  case Packet::ItemStackResponse: {
    // What the server did with a request to move stacks: which one, whether it
    // stood, and what the windows it named hold now.
    const std::uint32_t answers = readVarUInt(reader);
    std::string told;
    for (std::uint32_t at = 0; at < answers && !reader.failed(); ++at) {
      const std::uint8_t status = reader.readByte();
      const std::int32_t requestId = readVarInt(reader);
      told += (told.empty() ? "" : "; ") + std::string{"request "} +
              std::to_string(requestId) +
              (status == 0 ? " stood" : " was refused");
      bool windows = status == 0;
      if (options_.protocol >= 2168) {
        windows = reader.readByte() != 0 && reader.readByte() != 0;
      }
      if (!windows) {
        continue;
      }
      const std::uint32_t containers = readVarUInt(reader);
      for (std::uint32_t window = 0; window < containers && !reader.failed();
           ++window) {
        const std::uint8_t container = reader.readByte();
        if (reader.readByte() != 0) {
          static_cast<void>(readIntLE(reader));
        }
        const std::uint32_t slots = readVarUInt(reader);
        told += ", window " + std::to_string(container) + " has " +
                std::to_string(slots) + " slot(s)";
        for (std::uint32_t slot = 0; slot < slots && !reader.failed(); ++slot) {
          const std::uint8_t where = reader.readByte();
          static_cast<void>(
              reader.readByte()); // The same slot, as the hotbar numbers it,
          const std::uint8_t count = reader.readByte();
          if (options_.protocol >= 2168) {
            static_cast<void>(reader.readByte());
            if (reader.readByte() != 0) {
              static_cast<void>(readVarInt(reader));
            }
            static_cast<void>(readString(reader));
            if (reader.readByte() != 0) {
              static_cast<void>(readString(reader));
            }
          } else {
            static_cast<void>(readVarInt(reader));
            static_cast<void>(readString(reader));
            static_cast<void>(readString(reader));
          }
          static_cast<void>(readVarInt(reader)); // and how worn it is.
          told += " [" + std::to_string(where) + "]=" + std::to_string(count);
        }
      }
    }
    if (reader.failed()) {
      problem("an answer to a stack request ran out");
      return;
    }
    if (reader.remaining() != 0) {
      problem("an answer to a stack request left " +
              std::to_string(reader.remaining()) + " bytes unread");
      return;
    }
    say("stacks: " + told);
    return;
  }
  case Packet::InventoryContent: {
    // One window's worth of slots.
    const std::uint32_t containerId = readVarUInt(reader);
    const std::uint32_t slots = readVarUInt(reader);
    std::string held;
    std::int32_t filled = 0;
    for (std::uint32_t at = 0; at < slots && !reader.failed(); ++at) {
      const Stack stack = readWindowItem(reader, options_.protocol);
      if (stack.id == 0) {
        continue;
      }
      ++filled;
      held += (held.empty() ? "" : ", ") + ("[" + std::to_string(at) + "] ") +
              std::to_string(stack.count) + "x item " +
              std::to_string(stack.id) +
              (stack.data == 0 ? "" : "/" + std::to_string(stack.data));
    }
    static_cast<void>(reader.readByte()); // Which part of a window this is,
    if (reader.readByte() != 0) {
      static_cast<void>(readIntLE(reader)); // and its own id when it has one.
    }
    static_cast<void>(
        readWindowItem(reader, options_.protocol)); // What is stored beside it.
    if (reader.failed()) {
      problem("a window arrived with a body that ran out");
      return;
    }
    if (reader.remaining() != 0) {
      problem("a window left " + std::to_string(reader.remaining()) +
              " bytes unread");
      return;
    }
    carried_ += filled;
    say("window " + std::to_string(containerId) + " has " +
        std::to_string(slots) + " slot(s)" + (held.empty() ? "" : ": " + held));
    return;
  }
  case Packet::SetEntityData: {
    // Metadata for one entity, read field by field: a shape read wrong here is
    // a shape read wrong in every packet that carries metadata.
    static_cast<void>(readVarULong(reader));
    const std::int16_t air = readMetadata(reader, options_.protocol);
    if (air == -2) {
      problem("a metadata field named its format twice and said two different "
              "things");
      return;
    }
    if (air == -3) {
      problem("a metadata field arrived in a format this bot cannot read");
      return;
    }
    static_cast<void>(readVarUInt(
        reader)); // Entity properties, of which this server sends none.
    static_cast<void>(readVarUInt(reader));
    static_cast<void>(readVarULong(reader)); // Which tick it belongs to.
    if (reader.failed()) {
      problem("metadata arrived with a body that ran out");
      return;
    }
    if (reader.remaining() != 0) {
      problem("metadata left " + std::to_string(reader.remaining()) +
              " bytes unread");
      return;
    }
    if (air >= 0) {
      say("air is " + std::to_string(air));
    }
    return;
  }
  case Packet::EntityEvent: {
    // What an entity is doing that the client draws itself: the flash of a hit,
    // the fall of a death. Grew a trailing optional position at 975.
    static_cast<void>(readVarULong(reader));
    const std::uint8_t event = reader.readByte();
    static_cast<void>(readVarInt(reader));
    if (options_.protocol >= 975) {
      static_cast<void>(
          reader.readByte()); // Whether a position of its own follows.
    }
    if (reader.failed()) {
      problem("an entity event arrived with a body that ran out");
      return;
    }
    if (reader.remaining() != 0) {
      problem("an entity event left " + std::to_string(reader.remaining()) +
              " bytes unread");
      return;
    }
    ++hits_;
    say("entity event " + std::to_string(static_cast<std::int32_t>(event)));
    return;
  }
  case Packet::SetEntityMotion: {
    // Being thrown, which is what a hit does to whoever took it.
    static_cast<void>(readVarULong(reader));
    const float x = readFloatLE(reader);
    const float y = readFloatLE(reader);
    const float z = readFloatLE(reader);
    static_cast<void>(readVarULong(reader)); // Which tick it belongs to.
    if (reader.failed()) {
      problem("a motion arrived with a body that ran out");
      return;
    }
    if (reader.remaining() != 0) {
      problem("a motion left " + std::to_string(reader.remaining()) +
              " bytes unread");
      return;
    }
    say("thrown by " + std::to_string(x) + ", " + std::to_string(y) + ", " +
        std::to_string(z));
    return;
  }
  case Packet::SpawnParticleEffect: {
    // A particle, under the name Bedrock knows it by. Read to the last byte: a
    // shape that grew without us noticing is exactly what this bot is for.
    static_cast<void>(reader.readByte());   // Which dimension it belongs to.
    static_cast<void>(readVarLong(reader)); // Which entity, or -1 for none.
    const float x = readFloatLE(reader);
    static_cast<void>(readFloatLE(reader));
    static_cast<void>(readFloatLE(reader));
    const std::string name = readString(reader);
    static_cast<void>(
        reader.readByte()); // Whether variables of its own follow.
    if (reader.failed()) {
      problem("a particle arrived with a body that ran out");
      return;
    }
    if (reader.remaining() != 0) {
      problem("a particle left " + std::to_string(reader.remaining()) +
              " bytes unread");
      return;
    }
    ++particles_;
    say("saw the particle " + name + " around " +
        std::to_string(static_cast<std::int32_t>(x)));
    return;
  }
  case Packet::UpdateBlock: {
    // One block that changed where this client is already looking.
    const std::int32_t x = readVarInt(reader);
    // The height travels as an unsigned varint of the signed number, so it
    // comes back the size of a machine word when it is below zero and has to be
    // read as what it was.
    const auto y = static_cast<std::int32_t>(readVarUInt(reader));
    const std::int32_t z = readVarInt(reader);
    const std::uint32_t block = readVarUInt(reader);
    static_cast<void>(
        readVarUInt(reader)); // What the client should do about it,
    static_cast<void>(readVarUInt(reader)); // and on which layer.
    if (reader.failed()) {
      problem("a block change arrived with a body that ran out");
      return;
    }
    if (reader.remaining() != 0) {
      problem("a block change left " + std::to_string(reader.remaining()) +
              " bytes unread");
      return;
    }
    ++blockChanges_;
    say("a block changed at " + std::to_string(x) + ", " + std::to_string(y) +
        ", " + std::to_string(z) + " to id " + std::to_string(block));
    return;
  }
  case Packet::LevelChunk: {
    // How much world arrived. A column with no subchunks is a hole the client
    // draws as nothing, which is what this server sent before it knew the block
    // palette.
    static_cast<void>(readVarInt(reader)); // Which column,
    static_cast<void>(readVarInt(reader));
    static_cast<void>(readVarInt(reader)); // in which dimension,
    const std::uint32_t subChunks = readVarUInt(reader);
    if (reader.failed()) {
      return;
    }
    if (subChunks > 0) {
      ++columnsWithBlocks_;
      if (columnsWithBlocks_ == 1) {
        say("the first column with blocks has " + std::to_string(subChunks) +
            " subchunk(s)");
      }
    }
    return;
  }
  case Packet::ChangeDimension: {
    // Another world, which for this edition is another dimension. Read whole:
    // the loading screen id at the end is new enough that getting it wrong is
    // easy to miss.
    const std::int32_t dimension = readVarInt(reader);
    const float x = readFloatLE(reader);
    const float y = readFloatLE(reader);
    const float z = readFloatLE(reader);
    static_cast<void>(reader.readByte()); // Whether it is a respawn,
    static_cast<void>(
        reader.readByte()); // and whether a loading screen was named.
    if (reader.failed()) {
      problem("a dimension change arrived with a body that ran out");
      return;
    }
    if (reader.remaining() != 0) {
      problem("a dimension change left " + std::to_string(reader.remaining()) +
              " bytes unread");
      return;
    }
    x_ = x;
    y_ = y;
    z_ = z;
    say("moved to dimension " + std::to_string(dimension) + " at " +
        std::to_string(x) + ", " + std::to_string(y) + ", " +
        std::to_string(z));
    return;
  }
  case Packet::DeathInfo: {
    // What the death screen says. Bedrock puts that screen up by itself once
    // health reaches zero, so this only carries the sentence.
    const std::string cause = readString(reader);
    const std::uint32_t lines = readVarUInt(reader);
    if (reader.failed()) {
      problem("a death arrived with a body that ran out");
      return;
    }
    if (lines != 0) {
      problem("a death carries " + std::to_string(lines) +
              " extra lines, which this bot cannot read");
      return;
    }
    died_ = true;
    say("died: " + cause);
    // What a player does when they press the button, which is the only way
    // back.
    ByteWriter answer;
    writeFloatLE(answer, x_);
    writeFloatLE(answer, y_);
    writeFloatLE(answer, z_);
    answer.writeByte(2); // The client is ready.
    writeVarULong(answer, runtimeEntityId_);
    send(Packet::Respawn, answer.bytes());
    return;
  }
  case Packet::Respawn: {
    static_cast<void>(readFloatLE(reader));
    const float y = readFloatLE(reader);
    static_cast<void>(readFloatLE(reader));
    const std::uint8_t state = reader.readByte();
    if (reader.failed()) {
      return;
    }
    if (state == 1) {
      say("respawned at height " + std::to_string(y));
    }
    return;
  }
  case Packet::AvailableCommands: {
    // What this client is allowed to type. Read whole: a Bedrock client parses
    // a line against this list before it sends anything, so a name missing here
    // is a command the player cannot run at all, and a field of the wrong width
    // loses every name after it.
    for (int list = 0; list < 5; ++list) {
      // Enum values, subcommand words, suffixes, enums and subcommands: a
      // server with none of them writes five empty lists, and a bot that meets
      // one cannot read what follows.
      const std::uint32_t count = readVarUInt(reader);
      if (count != 0) {
        problem("the command list carries " + std::to_string(count) +
                " of list " + std::to_string(list) +
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
        for (std::uint32_t parameter = 0;
             parameter < parameters && !reader.failed(); ++parameter) {
          static_cast<void>(
              readString(reader));              // what the parameter is called,
          static_cast<void>(readIntLE(reader)); // what it is,
          static_cast<void>(reader.readByte()); // whether it may be left out,
          static_cast<void>(reader.readByte()); // and what else it carries.
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
      problem("the command list left " + std::to_string(reader.remaining()) +
              " bytes unread");
      return;
    }
    std::string named;
    for (const std::string &command : offered_) {
      named += (named.empty() ? "" : ", ") + command;
    }
    say("offered " + std::to_string(offered_.size()) + " command(s): " + named);
    return;
  }
  case Packet::LevelEvent: {
    // Something that happens to the world rather than to anybody. Only the
    // weather is read: the rest are effects a bot has nothing to draw with.
    const std::int32_t type = readVarInt(reader);
    const float x = readFloatLE(reader);
    const float y = readFloatLE(reader);
    const float z = readFloatLE(reader);
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
      // Everything else is an effect a bot has nothing to draw with, so it says
      // what it was told rather than drawing it: that is what a test of the
      // translation needs.
      say("effect " + std::to_string(type) + " at " + std::to_string(x) + ", " +
          std::to_string(y) + ", " + std::to_string(z) + " with " +
          std::to_string(data));
      break;
    }
    return;
  }
  case Packet::UpdateAttributes: {
    // What the HUD draws from. Bedrock is told the range of every number as
    // well as the number, so a value outside its own range is a server bug the
    // client would draw wrong.
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
        problem(name + " is " + std::to_string(value) +
                ", outside its own range");
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
    // Every id this bot does not decode, said once each: a packet nobody reads
    // is a packet nobody has checked, and a real client reads them all.
    if (unread_.insert(static_cast<std::int32_t>(packet)).second) {
      say("packet " + std::to_string(static_cast<std::int32_t>(packet)) +
          " arrived unread, " + std::to_string(reader.remaining()) +
          " byte(s)");
    }
    return;
  }
}

void BedrockBot::sendLogin() {
  const std::string identity =
      R"({"identityPublicKey":")" + keys_->publicKeyBase64() +
      R"(","extraData":{"displayName":"SwarmBot)" +
      std::to_string(options_.id) +
      R"(","identity":"00000000-0000-0000-0000-)" +
      std::string(12 - std::to_string(options_.id).size(), '0') +
      std::to_string(options_.id) + R"(","XUID":"25350000000000)" +
      std::to_string(options_.id % 100 + 10) + R"("}})";
  const auto chainJwt = keys_->signJwt(identity);
  const auto clientJwt = keys_->signJwt(
      R"({"DeviceOS":7,"DeviceModel":"Basalt Swarm","GameVersion":")" +
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
  const std::string auth = R"({"AuthenticationType":1,"Certificate":")" +
                           escaped + R"(","Token":""})";

  ByteWriter blob;
  writeIntLE(blob, static_cast<std::int32_t>(auth.size()));
  blob.writeBytes(
      {reinterpret_cast<const std::uint8_t *>(auth.data()), auth.size()});
  writeIntLE(blob, static_cast<std::int32_t>(clientJwt->size()));
  blob.writeBytes({reinterpret_cast<const std::uint8_t *>(clientJwt->data()),
                   clientJwt->size()});

  ByteWriter body;
  body.writeInt(options_.protocol);
  writeVarUInt(body, static_cast<std::uint32_t>(blob.size()));
  body.writeBytes(blob.bytes());
  send(Packet::Login, body.bytes());
}

void BedrockBot::sendPackResponse(const std::uint8_t status,
                                  const std::string &name) {
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

void BedrockBot::sendChat(const std::string &line) {
  ByteWriter body;
  body.writeByte(0); // Not a translation key.
  body.writeByte(1); // An author and a message, which is what a chat line is.
  body.writeByte(static_cast<std::uint8_t>(1)); // Chat.
  writeString(body, "SwarmBot" + std::to_string(options_.id));
  writeString(body, line);
  writeString(body, ""); // No xuid.
  writeString(body, ""); // No platform chat id.
  body.writeByte(0);     // Nothing filtered.
  send(Packet::Text, body.bytes());
}

void BedrockBot::sendAuthInput() {
  // One block action or one click, the first time round and never again: what
  // is being tested is that the server reads them, not that a bot can mine.
  const bool dig = !options_.digAt.empty() && !digSent_;
  // A click on nothing, the way a rod is cast and reeled in.
  const bool use = usesSent_ < options_.useCount && nowMillis() >= nextUseAt_;
  const bool put = (!options_.putAt.empty() && !putSent_) || use;
  const bool swap =
      (!options_.swapSlots.empty() || !options_.stashSlots.empty() ||
       !options_.takeSlots.empty()) &&
      !swapSent_;
  // Which windows the two ends are in: the carried 36, or whatever block is
  // open. A block's window is the 7 unless it is a cooker, which gives each of
  // its three slots a container of its own: --from and --into say which.
  const std::int32_t swapFromContainer =
      options_.takeSlots.empty()
          ? 12
          : (options_.fromContainer > 0 ? options_.fromContainer : 7);
  const std::int32_t swapInto =
      options_.stashSlots.empty()
          ? 12
          : (options_.intoContainer > 0 ? options_.intoContainer : 7);
  const std::array<std::int32_t, 3> at =
      pointOf(dig ? options_.digAt : options_.putAt);
  const std::string &asked = !options_.stashSlots.empty()  ? options_.stashSlots
                             : !options_.takeSlots.empty() ? options_.takeSlots
                                                           : options_.swapSlots;
  const std::array<std::int32_t, 3> slots = pointOf(asked + ",0");
  const std::int32_t swapFrom = slots[0];
  const std::int32_t swapTo = slots[1];
  digSent_ = digSent_ || dig;
  putSent_ = putSent_ || (put && !use);
  if (use) {
    ++usesSent_;
    nextUseAt_ = nowMillis() + options_.useEveryMillis;
    say("used what is in hand, " + std::to_string(usesSent_) + " of " +
        std::to_string(options_.useCount));
  }
  swapSent_ = swapSent_ || swap;

  ByteWriter body;
  writeFloatLE(body, 0.0F); // Pitch.
  writeFloatLE(body, 0.0F); // Yaw.
  writeFloatLE(body, x_);
  writeFloatLE(body, y_);
  writeFloatLE(body, z_);
  writeFloatLE(body, 0.0F); // Where it is trying to move, sideways.
  writeFloatLE(body, 0.0F); // The same, forwards.
  writeFloatLE(body, 0.0F); // Head yaw.
  // Which flags are on, which is what says the pieces below are there at all.
  if (options_.protocol >= 2168) {
    body.writeByte(1);
    writeVarUInt(body, static_cast<std::uint32_t>(
                           (dig ? 1 : 0) + (put ? 1 : 0) + (swap ? 1 : 0) +
                           (options_.sneaking ? 1 : 0) +
                           (options_.grounded ? 1 : 0)));
    if (options_.sneaking) {
      writeVarInt(body, 8);
    }
    if (options_.grounded) {
      writeVarInt(body, 50);
    }
    if (put) {
      writeVarInt(body, 34);
    }
    if (dig) {
      writeVarInt(body, 35);
    }
    if (swap) {
      writeVarInt(body, 36);
    }
  } else {
    // One long varint of bits. Fifty, standing on the ground, is the highest
    // this bot sets, so the run is eight bytes at most.
    std::uint64_t bits = 0;
    bits |= put ? (1ULL << 34U) : 0;
    bits |= dig ? (1ULL << 35U) : 0;
    bits |= swap ? (1ULL << 36U) : 0;
    bits |= options_.sneaking ? (1ULL << 8U) : 0;
    bits |= options_.grounded ? (1ULL << 50U) : 0;
    for (int part = 0; part < 8; ++part) {
      const std::uint8_t seven = static_cast<std::uint8_t>(
          (bits >> (7U * static_cast<std::uint32_t>(part))) & 0x7fU);
      const bool more =
          (bits >> (7U * static_cast<std::uint32_t>(part + 1))) != 0;
      body.writeByte(static_cast<std::uint8_t>(seven | (more ? 0x80U : 0U)));
      if (!more) {
        break;
      }
    }
  }
  writeVarUInt(body, 0);    // Input mode: none.
  writeVarUInt(body, 0);    // Play mode: normal.
  writeVarInt(body, 0);     // Interaction model: touch.
  writeFloatLE(body, 0.0F); // Interact rotation.
  writeFloatLE(body, 0.0F);
  writeVarULong(body, tick_++);
  writeFloatLE(body, 0.0F); // Delta.
  writeFloatLE(body, 0.0F);
  writeFloatLE(body, 0.0F);
  // The five optional pieces. From 2168 each is a pair of booleans, the first
  // saying the field is there at all; below it they are not written, and a flag
  // alone decides.
  const auto optional = [this, &body](const bool present) {
    if (options_.protocol >= 2168) {
      body.writeByte(1);
      body.writeByte(present ? 1 : 0);
      return present;
    }
    return present;
  };
  const bool native = options_.protocol >= 2168;
  if (optional(put)) {
    writeVarInt(body, 0); // No legacy request,
    if (native) {
      body.writeByte(0); // no slots left over from one,
      body.writeByte(1); // and a list of moves that is there
      body.writeByte(1);
    }
    writeVarUInt(body, 0); // but empty.
    // A click on a block, or on nothing, set off by a hand rather than by the
    // server asking again.
    const std::int32_t action = use ? 1 : 0;
    if (native) {
      writeVarInt(body, action);
      body.writeByte(0);
    } else {
      writeVarUInt(body, static_cast<std::uint32_t>(action));
      writeVarUInt(body, 0);
    }
    writeVarInt(body, at[0]);
    writeVarUInt(body, static_cast<std::uint32_t>(at[1]));
    writeVarInt(body, at[2]);
    if (native) {
      body.writeByte(1); // On its top,
    } else {
      writeVarInt(body, 1);
    }
    writeVarInt(body, 0); // with what is in the first slot.
    // What is in hand, which for this bot is the first item the server named:
    // enough for the server to see an id it gave out come back. From 2168 the
    // id is a short.
    if (native) {
      writeShortLE(body, 1);
    } else {
      writeVarInt(body, 1);
    }
    writeShortLE(body, 1);  // One of them,
    writeVarUInt(body, 0);  // undamaged,
    body.writeByte(0);      // with no net id,
    writeVarUInt(body, 0);  // placing no block of its own
    writeVarUInt(body, 0);  // and nothing written on it.
    writeFloatLE(body, x_); // Where this bot is,
    writeFloatLE(body, y_);
    writeFloatLE(body, z_);
    writeFloatLE(body, 0.5F); // and where on the face it clicked.
    writeFloatLE(body, 1.0F);
    writeFloatLE(body, 0.5F);
    writeVarUInt(body, 0); // Which block it thinks is there,
    if (native) {
      body.writeByte(0); // and what it thinks the click did.
    } else {
      writeVarUInt(body, 0);
    }
    if (options_.protocol >= 944) {
      body.writeByte(0); // Nothing on cooldown.
    }
  }
  if (optional(swap)) {
    // Either the two slots trade places, or a count is carried from one to the
    // other through the cursor, which is the pair of lines a real client sends
    // for a click and a click.
    const std::int32_t carried = slots[2];
    // Take, place and swap are the first three on both numberings.
    constexpr std::int32_t kTakeLine = 0;
    constexpr std::int32_t kPlaceLine = 1;
    constexpr std::int32_t kSwapLine = 2;
    writeVarInt(body, ++requestId_);
    writeVarUInt(body, carried > 0 ? 2 : 1);
    const auto line = [&body, native](const std::int32_t id) {
      if (native) {
        writeVarUInt(body, static_cast<std::uint32_t>(id));
      }
      body.writeByte(static_cast<std::uint8_t>(id));
    };
    if (carried > 0) {
      line(kTakeLine);
      body.writeByte(static_cast<std::uint8_t>(carried));
      writeStackSlot(body, swapFromContainer, swapFrom, options_.protocol);
      writeStackSlot(body, 59, 0, options_.protocol);
      line(kPlaceLine);
      body.writeByte(static_cast<std::uint8_t>(carried));
      writeStackSlot(body, 59, 0, options_.protocol);
      writeStackSlot(body, swapInto, swapTo, options_.protocol);
    } else {
      line(kSwapLine);
      writeStackSlot(body, swapFromContainer, swapFrom, options_.protocol);
      writeStackSlot(body, swapInto, swapTo, options_.protocol);
    }
    writeVarUInt(body, 0); // Nothing typed where a server may filter it,
    writeIntLE(body, -1);  // and nothing said about where it came from.
  }
  if (optional(dig)) {
    if (options_.protocol >= 2168) {
      writeVarUInt(body, 1);
    } else {
      writeVarInt(body, 1);
    }
    writeVarInt(body, 26); // Broken, as the client predicts it.
    writeVarInt(body, at[0]);
    writeVarInt(body, at[1]);
    writeVarInt(body, at[2]);
    writeVarInt(body, 1); // Hit from above.
  }
  static_cast<void>(optional(false)); // Riding nothing,
  static_cast<void>(optional(false)); // and predicting nothing about it.
  writeFloatLE(body, 0.0F);           // Analog move.
  writeFloatLE(body, 0.0F);
  writeFloatLE(body, 0.0F); // Camera orientation.
  writeFloatLE(body, 0.0F);
  writeFloatLE(body, 0.0F);
  writeFloatLE(body, 0.0F); // Raw move.
  writeFloatLE(body, 0.0F);
  send(Packet::PlayerAuthInput, body.bytes());
}

void BedrockBot::sendCommand(const std::string &line) {
  ByteWriter body;
  writeString(body, "/" + line);
  // Where the client says the command came from: its own name for the kind of
  // console, who is asking, the request it belongs to, and which entity that
  // is.
  writeString(body, "player");
  for (int index = 0; index < 16; ++index) {
    body.writeByte(0);
  }
  writeString(body, "");
  for (int index = 0; index < 8; ++index) {
    body.writeByte(0);
  }
  body.writeByte(0);     // Not the server talking to itself.
  writeString(body, ""); // The version of the command syntax, which a server
                         // reads from its own tree.
  send(Packet::CommandRequest, body.bytes());
}

void BedrockBot::onHandshake(ByteReader &reader) {
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
  say("server list: " + advertisement->motd + " (" + advertisement->version +
      ", protocol " + std::to_string(advertisement->protocol) + ", " +
      std::to_string(advertisement->online) + "/" +
      std::to_string(advertisement->max) + ")");
  if (advertisement->protocol != options_.protocol) {
    say("the server speaks protocol " +
        std::to_string(advertisement->protocol) + " and this bot claims " +
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
    say("in the world with " +
        std::to_string(
            counts_[static_cast<std::uint16_t>(Packet::LevelChunk)]) +
        " chunk(s)");
  }
  if (spawned_) {
    // What a client sends the moment it is in: it is initialised, and then it
    // starts telling the server where it is, every tick, for as long as it is
    // there.
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
    // A real client says goodbye to a window before it leaves, and the server
    // owes it an answer: sending one with time to spare is what tests that path
    // at all.
    const std::int64_t closeAt = leaveAt - (options_.stayMillis / 3);
    std::int64_t nextStep = nowMillis();
    bool closeSent = false;
    while (running_ && nowMillis() < leaveAt) {
      if (!pump(100)) {
        break;
      }
      if (!closeSent && openContainer_ >= 0 && nowMillis() >= closeAt) {
        ByteWriter close;
        close.writeByte(static_cast<std::uint8_t>(openContainer_));
        close.writeByte(0); // A plain container,
        close.writeByte(0); // closed by the client rather than by the server.
        send(Packet::ContainerClose, close.bytes());
        closeSent = true;
      }
      if (nowMillis() >= nextStep) {
        // A step east every tenth of a second, which is about what a walking
        // player does.
        x_ += 0.2F;
        sendAuthInput();
        nextStep = nowMillis() + 100;
      }
    }
    say("left with " + std::to_string(columnsWithBlocks_) +
        " column(s) of the world drawn, of " +
        std::to_string(
            counts_[static_cast<std::uint16_t>(Packet::LevelChunk)]) +
        " sent");
  }
  rak_.disconnect();
}

} // namespace basalt::swarm::bedrock
