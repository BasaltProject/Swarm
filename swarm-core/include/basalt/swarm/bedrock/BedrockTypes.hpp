#pragma once

#include <basalt/swarm/Bytes.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace basalt::swarm::bedrock {

/// The Bedrock protocol this bot claims by default. A server that speaks
/// another answers with the number it wants instead of letting the bot in, and
/// that is a thing worth testing too.
inline constexpr int kBedrockProtocol = 2168;

inline constexpr std::string_view kBedrockVersion = "1.26.40";

/// Bedrock writes its fixed-width numbers little endian and its variable ones
/// LEB128, which is the opposite of Java Edition on both counts. None of the
/// readers a Java bot uses fit here, so these are their own.

inline void writeVarUInt(ByteWriter& writer, std::uint32_t value) {
    while (value >= 0x80U) {
        writer.writeByte(static_cast<std::uint8_t>(value | 0x80U));
        value >>= 7U;
    }
    writer.writeByte(static_cast<std::uint8_t>(value));
}

inline void writeVarInt(ByteWriter& writer, std::int32_t value) {
    writeVarUInt(writer, static_cast<std::uint32_t>((value << 1) ^ (value >> 31)));
}

inline void writeShortLE(ByteWriter& writer, std::uint16_t value) {
    writer.writeByte(static_cast<std::uint8_t>(value & 0xffU));
    writer.writeByte(static_cast<std::uint8_t>(value >> 8U));
}

inline void writeFloatLE(ByteWriter& writer, float value) {
    std::uint32_t raw = 0;
    static_assert(sizeof(raw) == sizeof(value));
    __builtin_memcpy(&raw, &value, sizeof(raw));
    for (int shift = 0; shift < 32; shift += 8) {
        writer.writeByte(static_cast<std::uint8_t>((raw >> shift) & 0xffU));
    }
}

inline void writeVarULong(ByteWriter& writer, std::uint64_t value) {
    while (value >= 0x80U) {
        writer.writeByte(static_cast<std::uint8_t>(value | 0x80U));
        value >>= 7U;
    }
    writer.writeByte(static_cast<std::uint8_t>(value));
}

[[nodiscard]] inline std::uint64_t readVarULong(ByteReader& reader) {
    std::uint64_t result = 0;
    for (int index = 0; index < 10; ++index) {
        const std::uint8_t part = reader.readByte();
        result |= static_cast<std::uint64_t>(part & 0x7fU) << (7 * index);
        if ((part & 0x80U) == 0) {
            return result;
        }
    }
    return result;
}

[[nodiscard]] inline std::int64_t readVarLong(ByteReader& reader) {
    const std::uint64_t zigzag = readVarULong(reader);
    return static_cast<std::int64_t>((zigzag >> 1U) ^ (~(zigzag & 1U) + 1U));
}

inline void writeIntLE(ByteWriter& writer, std::int32_t value) {
    const auto raw = static_cast<std::uint32_t>(value);
    for (int shift = 0; shift < 32; shift += 8) {
        writer.writeByte(static_cast<std::uint8_t>((raw >> shift) & 0xffU));
    }
}

inline void writeString(ByteWriter& writer, std::string_view value) {
    writeVarUInt(writer, static_cast<std::uint32_t>(value.size()));
    writer.writeBytes({reinterpret_cast<const std::uint8_t*>(value.data()), value.size()});
}

[[nodiscard]] inline std::uint32_t readVarUInt(ByteReader& reader) {
    std::uint32_t result = 0;
    for (int index = 0; index < 5; ++index) {
        const std::uint8_t part = reader.readByte();
        result |= static_cast<std::uint32_t>(part & 0x7fU) << (7 * index);
        if ((part & 0x80U) == 0) {
            return result;
        }
    }
    return result;
}

[[nodiscard]] inline std::int32_t readVarInt(ByteReader& reader) {
    const std::uint32_t zigzag = readVarUInt(reader);
    return static_cast<std::int32_t>((zigzag >> 1U) ^ (~(zigzag & 1U) + 1U));
}

[[nodiscard]] inline float readFloatLE(ByteReader& reader) {
    std::uint32_t raw = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        raw |= static_cast<std::uint32_t>(reader.readByte()) << shift;
    }
    float value = 0.0F;
    __builtin_memcpy(&value, &raw, sizeof(value));
    return value;
}

[[nodiscard]] inline std::uint16_t readShortLE(ByteReader& reader) {
    const std::uint16_t low = reader.readByte();
    const std::uint16_t high = reader.readByte();
    return static_cast<std::uint16_t>(low | static_cast<std::uint16_t>(high << 8U));
}

[[nodiscard]] inline std::string readString(ByteReader& reader) {
    const std::uint32_t length = readVarUInt(reader);
    if (reader.failed() || length > reader.remaining()) {
        return {};
    }
    std::string text;
    text.reserve(length);
    for (std::uint32_t index = 0; index < length; ++index) {
        text += static_cast<char>(reader.readByte());
    }
    return text;
}

[[nodiscard]] inline std::int32_t readIntLE(ByteReader& reader) {
    std::uint32_t raw = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        raw |= static_cast<std::uint32_t>(reader.readByte()) << shift;
    }
    return static_cast<std::int32_t>(raw);
}

[[nodiscard]] inline std::int64_t readLongLE(ByteReader& reader) {
    std::uint64_t raw = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        raw |= static_cast<std::uint64_t>(reader.readByte()) << shift;
    }
    return static_cast<std::int64_t>(raw);
}

/// A uuid, as Bedrock writes one: two little-endian longs, most significant
/// first. Printed the way anybody would recognise it.
[[nodiscard]] inline std::string readUuid(ByteReader& reader) {
    const auto high = static_cast<std::uint64_t>(readLongLE(reader));
    const auto low = static_cast<std::uint64_t>(readLongLE(reader));
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string text;
    for (int nibble = 15; nibble >= 0; --nibble) {
        text += kDigits[(high >> (nibble * 4)) & 0xfU];
    }
    for (int nibble = 15; nibble >= 0; --nibble) {
        text += kDigits[(low >> (nibble * 4)) & 0xfU];
    }
    return text;
}

/// Protocol 2168 rewrote the skin, the player list and the two resource pack
/// packets. A bot that claims anything older in the window reads and writes the
/// shape underneath.
[[nodiscard]] inline bool nativeShapes(const int protocol) { return protocol >= kBedrockProtocol; }

/// A network NBT tag, skipped rather than read: a bot only needs to get past
/// it. Bedrock writes its NBT little endian with varint lengths, and the only
/// one a server sends here is empty.
inline void skipTag(ByteReader& reader) {
    const std::uint8_t kind = reader.readByte();
    if (kind != 0x0a) {
        return; // Not a compound, and nothing else is expected where this is
                // called.
    }
    reader.skip(readVarUInt(reader)); // Its name, which is never given one.
    while (!reader.failed() && reader.readByte() != 0x00) {
        // A field this bot cannot read means the rest of the packet is unreadable
        // too, and the caller finds that out when it runs off the end.
        return;
    }
}

/// An image inside a skin: its size, then the pixels, with no header at all.
inline void skipImage(ByteReader& reader) {
    static_cast<void>(readIntLE(reader));
    static_cast<void>(readIntLE(reader));
    reader.skip(readVarUInt(reader));
}

/// The skin a player list carries. Read in full rather than skipped: the fields
/// after it only land in the right place if every one of these did.
inline void skipSkin(ByteReader& reader, const int protocol) {
    if (!nativeShapes(protocol)) {
        static_cast<void>(readString(reader)); // Skin id,
        static_cast<void>(readString(reader)); // the PlayFab account,
        static_cast<void>(readString(reader)); // and the geometry's name.
        skipImage(reader);
        const std::int32_t animations = readIntLE(reader);
        for (std::int32_t index = 0; index < animations && !reader.failed(); ++index) {
            skipImage(reader);
            static_cast<void>(readVarUInt(reader));
            static_cast<void>(readFloatLE(reader));
            static_cast<void>(readVarUInt(reader));
        }
        skipImage(reader);                     // The cape,
        static_cast<void>(readString(reader)); // the geometry,
        static_cast<void>(readString(reader)); // what engine wrote it,
        static_cast<void>(readString(reader)); // the animations,
        static_cast<void>(readString(reader)); // the cape's id,
        static_cast<void>(readString(reader)); // the id of the lot,
        static_cast<void>(readString(reader)); // which arms,
        static_cast<void>(readString(reader)); // and what colour.
        const std::int32_t pieces = readIntLE(reader);
        for (std::int32_t index = 0; index < pieces && !reader.failed(); ++index) {
            for (int field = 0; field < 3; ++field) {
                static_cast<void>(readString(reader));
            }
            static_cast<void>(reader.readByte());
            static_cast<void>(readString(reader));
        }
        const std::int32_t tints = readIntLE(reader);
        for (std::int32_t index = 0; index < tints && !reader.failed(); ++index) {
            static_cast<void>(readString(reader));
            const std::int32_t colours = readIntLE(reader);
            for (std::int32_t colour = 0; colour < colours && !reader.failed(); ++colour) {
                static_cast<void>(readString(reader));
            }
        }
        for (int flag = 0; flag < 5; ++flag) {
            static_cast<void>(reader.readByte());
        }
        return;
    }

    static_cast<void>(readString(reader)); // Skin id,
    static_cast<void>(readString(reader)); // the PlayFab account behind it,
    static_cast<void>(readString(reader)); // and the json naming its geometry.
    skipImage(reader);
    const std::uint32_t animations = readVarUInt(reader);
    for (std::uint32_t index = 0; index < animations && !reader.failed(); ++index) {
        skipImage(reader);
        static_cast<void>(readVarUInt(reader)); // What the texture is of,
        static_cast<void>(readFloatLE(reader)); // how many frames,
        static_cast<void>(readVarUInt(reader)); // and what it expresses.
    }
    skipImage(reader);                     // The cape,
    static_cast<void>(readString(reader)); // the geometry itself,
    static_cast<void>(readString(reader)); // what engine wrote it,
    static_cast<void>(readString(reader)); // the animations,
    static_cast<void>(readString(reader)); // the cape's id
    static_cast<void>(readString(reader)); // and the id of the lot.
    static_cast<void>(reader.readByte());  // Which arms,
    static_cast<void>(readIntLE(reader));  // and what colour.
    const std::uint32_t pieces = readVarUInt(reader);
    for (std::uint32_t index = 0; index < pieces && !reader.failed(); ++index) {
        static_cast<void>(readString(reader));
        static_cast<void>(readIntLE(reader));
        static_cast<void>(readUuid(reader));
        static_cast<void>(reader.readByte());
        static_cast<void>(readString(reader));
    }
    const std::uint32_t tints = readVarUInt(reader);
    for (std::uint32_t index = 0; index < tints && !reader.failed(); ++index) {
        static_cast<void>(readString(reader));
        for (int colour = 0; colour < 4; ++colour) {
            static_cast<void>(readIntLE(reader));
        }
    }
    for (int flag = 0; flag < 5; ++flag) {
        static_cast<void>(reader.readByte());
    }
    static_cast<void>(readString(reader)); // Whether it is trusted, spelled out,
    static_cast<void>(readString(reader)); // and the hash of the profile it came from.
}

/// The packet ids a bot needs. Every live Bedrock protocol numbers them the
/// same way.
enum class Packet : std::uint16_t {
    Login = 1,
    PlayStatus = 2,
    ServerToClientHandshake = 3,
    ClientToServerHandshake = 4,
    Disconnect = 5,
    SetTime = 10,
    Text = 9,
    ResourcePacksInfo = 6,
    ResourcePackStack = 7,
    ResourcePackClientResponse = 8,
    StartGame = 11,
    LevelEvent = 25,
    AddPlayer = 12,
    AddEntity = 13,
    BossEvent = 74,
    RemoveObjective = 106,
    SetDisplayObjective = 107,
    SetScore = 108,
    AddItemEntity = 15,
    RemoveEntity = 14,
    MoveEntityAbsolute = 18,
    UpdateAttributes = 29,
    MovePlayer = 19,
    SetPlayerGameType = 62,
    SetTitle = 88,
    CommandRequest = 77,
    ModalFormRequest = 100,
    ModalFormResponse = 101,
    LevelChunk = 58,
    RequestChunkRadius = 69,
    ChunkRadiusUpdated = 70,
    SetLocalPlayerAsInitialized = 113,
    ChangeDimension = 61,
    UpdateBlock = 21,
    PlaySound = 86,
    SpawnParticleEffect = 118,
    MobEffect = 28,
    EntityEvent = 27,
    SetEntityMotion = 40,
    SetEntityData = 39,
    ItemComponent = 162,
    BlockEvent = 26,
    ContainerOpen = 46,
    ContainerClose = 47,
    InventoryContent = 49,
    ContainerSetData = 51,
    ItemStackResponse = 148,
    CreativeContent = 145,
    Respawn = 45,
    AvailableCommands = 76,
    DeathInfo = 189,
    MobEquipment = 31,
    PlayerList = 63,
    NetworkChunkPublisherUpdate = 121,
    NetworkSettings = 143,
    PlayerAuthInput = 144,
    RequestNetworkSettings = 193,
};

} // namespace basalt::swarm::bedrock
