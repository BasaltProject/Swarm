#include <basalt/swarm/Bytes.hpp>

#include <algorithm>
#include <bit>
#include <cstring>

namespace basalt::swarm {
namespace {

/// parity: a VarInt is five bytes at most, and a longer one is a malformed packet rather than a
/// bigger number.
constexpr std::int32_t kMaxVarIntBytes = 5;

} // namespace

std::uint8_t ByteReader::readByte() {
    if (at_ >= bytes_.size()) {
        failed_ = true;
        return 0;
    }
    return bytes_[at_++];
}

std::int16_t ByteReader::readShort() {
    const std::uint32_t high = readByte();
    const std::uint32_t low = readByte();
    return static_cast<std::int16_t>((high << 8U) | low);
}

std::int32_t ByteReader::readInt() {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value = (value << 8U) | readByte();
    }
    return static_cast<std::int32_t>(value);
}

std::int64_t ByteReader::readLong() {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8U) | readByte();
    }
    return static_cast<std::int64_t>(value);
}

float ByteReader::readFloat() { return std::bit_cast<float>(static_cast<std::uint32_t>(readInt())); }

double ByteReader::readDouble() { return std::bit_cast<double>(static_cast<std::uint64_t>(readLong())); }

std::int32_t ByteReader::readVarInt() {
    std::uint32_t result = 0;
    for (std::int32_t read = 0; read < kMaxVarIntBytes; ++read) {
        const std::uint8_t byte = readByte();
        result |= static_cast<std::uint32_t>(byte & 0x7FU) << (7U * static_cast<std::uint32_t>(read));
        if ((byte & 0x80U) == 0) {
            return static_cast<std::int32_t>(result);
        }
        if (failed_) {
            return 0;
        }
    }
    failed_ = true;
    return 0;
}

std::string ByteReader::readVarString() {
    const std::int32_t length = readVarInt();
    if (length < 0 || static_cast<std::size_t>(length) > remaining()) {
        failed_ = true;
        return {};
    }
    const auto size = static_cast<std::size_t>(length);
    std::string value(reinterpret_cast<const char*>(bytes_.data() + at_), size);
    at_ += size;
    return value;
}

void ByteReader::skip(const std::size_t count) {
    if (count > remaining()) {
        failed_ = true;
        at_ = bytes_.size();
        return;
    }
    at_ += count;
}

std::span<const std::uint8_t> ByteReader::rest() {
    const std::span<const std::uint8_t> left = bytes_.subspan(at_);
    at_ = bytes_.size();
    return left;
}

void ByteWriter::writeShort(const std::int16_t value) {
    const auto raw = static_cast<std::uint16_t>(value);
    writeByte(static_cast<std::uint8_t>(raw >> 8U));
    writeByte(static_cast<std::uint8_t>(raw));
}

void ByteWriter::writeInt(const std::int32_t value) {
    const auto raw = static_cast<std::uint32_t>(value);
    for (int shift = 24; shift >= 0; shift -= 8) {
        writeByte(static_cast<std::uint8_t>(raw >> static_cast<std::uint32_t>(shift)));
    }
}

void ByteWriter::writeLong(const std::int64_t value) {
    const auto raw = static_cast<std::uint64_t>(value);
    for (int shift = 56; shift >= 0; shift -= 8) {
        writeByte(static_cast<std::uint8_t>(raw >> static_cast<std::uint32_t>(shift)));
    }
}

void ByteWriter::writeFloat(const float value) { writeInt(static_cast<std::int32_t>(std::bit_cast<std::uint32_t>(value))); }

void ByteWriter::writeDouble(const double value) {
    writeLong(static_cast<std::int64_t>(std::bit_cast<std::uint64_t>(value)));
}

void ByteWriter::writeVarInt(const std::int32_t value) {
    auto remaining = static_cast<std::uint32_t>(value);
    while (true) {
        if ((remaining & ~0x7FU) == 0) {
            writeByte(static_cast<std::uint8_t>(remaining));
            return;
        }
        writeByte(static_cast<std::uint8_t>((remaining & 0x7FU) | 0x80U));
        remaining >>= 7U;
    }
}

void ByteWriter::writeVarString(const std::string_view value) {
    writeVarInt(static_cast<std::int32_t>(value.size()));
    bytes_.insert(bytes_.end(), value.begin(), value.end());
}

void ByteWriter::writeBytes(const std::span<const std::uint8_t> value) {
    bytes_.insert(bytes_.end(), value.begin(), value.end());
}

} // namespace basalt::swarm
