#pragma once

#include <basalt/swarm/Bytes.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace basalt::swarm::bedrock {

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

/// The packet ids a bot needs. Every live Bedrock protocol numbers them the
/// same way.
enum class Packet : std::uint16_t {
    Login = 1,
    PlayStatus = 2,
    ServerToClientHandshake = 3,
    ClientToServerHandshake = 4,
    Disconnect = 5,
    Text = 9,
    ResourcePacksInfo = 6,
    ResourcePackStack = 7,
    ResourcePackClientResponse = 8,
    StartGame = 11,
    LevelChunk = 58,
    RequestChunkRadius = 69,
    ChunkRadiusUpdated = 70,
    SetLocalPlayerAsInitialized = 113,
    NetworkChunkPublisherUpdate = 121,
    NetworkSettings = 143,
    PlayerAuthInput = 144,
    RequestNetworkSettings = 193,
};

} // namespace basalt::swarm::bedrock
