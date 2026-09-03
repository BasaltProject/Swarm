#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace basalt::swarm {

/// Reads a packet body without ever throwing.
///
/// A read past the end answers zero and raises a sticky flag instead of failing on the spot. The
/// caller looks at `failed` once, after the packet, which is the only place it can do anything
/// useful about it: a malformed packet costs that packet and not the connection.
class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool failed() const noexcept { return failed_; }

    [[nodiscard]] std::size_t remaining() const noexcept { return bytes_.size() - at_; }

    [[nodiscard]] std::uint8_t readByte();

    [[nodiscard]] std::int16_t readShort();

    [[nodiscard]] std::int32_t readInt();

    [[nodiscard]] std::int64_t readLong();

    [[nodiscard]] float readFloat();

    [[nodiscard]] double readDouble();

    /// parity: the protocol's own variable-length int, five bytes at most.
    [[nodiscard]] std::int32_t readVarInt();

    /// A length-prefixed UTF-8 string.
    [[nodiscard]] std::string readVarString();

    /// Walks past `count` bytes, or to the end when there are fewer.
    void skip(std::size_t count);

    /// Everything left, which is how a bot discards the rest of a packet it does not read.
    [[nodiscard]] std::span<const std::uint8_t> rest();

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t at_ = 0;
    bool failed_ = false;
};

/// Builds a packet body. Nothing here can fail: it only ever grows.
class ByteWriter {
public:
    void writeByte(std::uint8_t value) { bytes_.push_back(value); }

    void writeShort(std::int16_t value);

    void writeInt(std::int32_t value);

    void writeLong(std::int64_t value);

    void writeFloat(float value);

    void writeDouble(double value);

    void writeVarInt(std::int32_t value);

    void writeVarString(std::string_view value);

    void writeBytes(std::span<const std::uint8_t> value);

    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return bytes_; }

    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }

private:
    std::vector<std::uint8_t> bytes_;
};

} // namespace basalt::swarm
