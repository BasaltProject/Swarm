#include <basalt/swarm/Bytes.hpp>
#include <basalt/swarm/Nbt.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace basalt::swarm::nbt {
namespace {

constexpr std::int32_t kEnd = 0;
constexpr std::int32_t kByte = 1;
constexpr std::int32_t kShort = 2;
constexpr std::int32_t kInt = 3;
constexpr std::int32_t kLong = 4;
constexpr std::int32_t kFloat = 5;
constexpr std::int32_t kDouble = 6;
constexpr std::int32_t kByteArray = 7;
constexpr std::int32_t kString = 8;
constexpr std::int32_t kList = 9;
constexpr std::int32_t kCompound = 10;
constexpr std::int32_t kIntArray = 11;
constexpr std::int32_t kLongArray = 12;

/// A tag nested this deep is a malformed packet, not a tree: the recursion stops rather than the
/// stack does.
constexpr std::int32_t kMaxDepth = 512;

void skipPayload(ByteReader& reader, std::int32_t type, std::int32_t depth);

void skipCount(ByteReader& reader, const std::int32_t count, const std::int32_t elements, const std::int32_t depth) {
    for (std::int32_t i = 0; i < count && !reader.failed(); ++i) {
        if (elements != kEnd) {
            skipPayload(reader, elements, depth + 1);
        }
    }
}

void skipPayload(ByteReader& reader, const std::int32_t type, const std::int32_t depth) {
    if (depth > kMaxDepth) {
        reader.skip(reader.remaining() + 1);
        return;
    }
    switch (type) {
    case kByte:
        reader.skip(1);
        return;
    case kShort:
        reader.skip(2);
        return;
    case kInt:
    case kFloat:
        reader.skip(4);
        return;
    case kLong:
    case kDouble:
        reader.skip(8);
        return;
    case kByteArray:
        reader.skip(static_cast<std::size_t>(std::max(reader.readInt(), 0)));
        return;
    case kString:
        reader.skip(static_cast<std::size_t>(static_cast<std::uint16_t>(reader.readShort())));
        return;
    case kList: {
        const std::int32_t elements = reader.readByte();
        skipCount(reader, reader.readInt(), elements, depth);
        return;
    }
    case kCompound:
        while (!reader.failed()) {
            const std::int32_t entry = reader.readByte();
            if (entry == kEnd) {
                return;
            }
            reader.skip(static_cast<std::size_t>(static_cast<std::uint16_t>(reader.readShort())));
            skipPayload(reader, entry, depth + 1);
        }
        return;
    case kIntArray:
        reader.skip(static_cast<std::size_t>(std::max(reader.readInt(), 0)) * 4);
        return;
    case kLongArray:
        reader.skip(static_cast<std::size_t>(std::max(reader.readInt(), 0)) * 8);
        return;
    default:
        // A tag id this does not have is a packet this cannot walk, so it stops reading rather than
        // guessing a length.
        reader.skip(reader.remaining() + 1);
        return;
    }
}

/// Reads one name, which in NBT is a length and then the bytes.
[[nodiscard]] std::string readName(ByteReader& reader) {
    const auto length = static_cast<std::size_t>(static_cast<std::uint16_t>(reader.readShort()));
    std::string name;
    name.reserve(length);
    for (std::size_t index = 0; index < length && !reader.failed(); ++index) {
        name += static_cast<char>(reader.readByte());
    }
    return name;
}

/// Walks a compound, naming its members, and descends one more level into the compounds inside it.
void walkCompound(ByteReader& reader, const std::function<void(std::string_view)>& onKey, const std::string& prefix,
                  const std::int32_t levelsLeft) {
    while (!reader.failed()) {
        const std::int32_t entry = reader.readByte();
        if (entry == kEnd) {
            return;
        }
        const std::string name = readName(reader);
        const std::string path = prefix.empty() ? name : prefix + "." + name;
        onKey(path);
        if (entry == kCompound && levelsLeft > 0) {
            walkCompound(reader, onKey, path, levelsLeft - 1);
            continue;
        }
        skipPayload(reader, entry, 1);
    }
}

/// Collects the words of a component compound, descending into the lists that hold more of them.
void readText(ByteReader& reader, std::string& into, const std::int32_t depth) {
    if (depth > kMaxDepth) {
        reader.skip(reader.remaining() + 1);
        return;
    }
    while (!reader.failed()) {
        const std::int32_t entry = reader.readByte();
        if (entry == kEnd) {
            return;
        }
        const std::string name = readName(reader);
        if (entry == kString && (name == "text" || name == "translate")) {
            const auto length = static_cast<std::size_t>(static_cast<std::uint16_t>(reader.readShort()));
            for (std::size_t index = 0; index < length && !reader.failed(); ++index) {
                into += static_cast<char>(reader.readByte());
            }
            continue;
        }
        // What a line is made of: the pieces after it, and what was filled into a translation.
        if (entry == kList && (name == "extra" || name == "with")) {
            const std::int32_t elements = reader.readByte();
            const std::int32_t count = reader.readInt();
            for (std::int32_t index = 0; index < count && !reader.failed(); ++index) {
                if (elements == kCompound) {
                    if (!into.empty()) {
                        into += " ";
                    }
                    readText(reader, into, depth + 1);
                    continue;
                }
                if (elements == kString) {
                    const auto length = static_cast<std::size_t>(static_cast<std::uint16_t>(reader.readShort()));
                    if (!into.empty()) {
                        into += " ";
                    }
                    for (std::size_t at = 0; at < length && !reader.failed(); ++at) {
                        into += static_cast<char>(reader.readByte());
                    }
                    continue;
                }
                if (elements != kEnd) {
                    skipPayload(reader, elements, depth + 1);
                }
            }
            continue;
        }
        skipPayload(reader, entry, depth + 1);
    }
}

} // namespace

std::string text(ByteReader& reader) {
    const std::int32_t type = reader.readByte();
    if (type != kCompound) {
        if (type == kString) {
            std::string plain;
            const auto length = static_cast<std::size_t>(static_cast<std::uint16_t>(reader.readShort()));
            for (std::size_t index = 0; index < length && !reader.failed(); ++index) {
                plain += static_cast<char>(reader.readByte());
            }
            return plain;
        }
        if (type != kEnd) {
            skipPayload(reader, type, 0);
        }
        return {};
    }
    std::string words;
    readText(reader, words, 0);
    return words;
}

void walkKeys(ByteReader& reader, const std::function<void(std::string_view path)>& onKey) {
    const std::int32_t type = reader.readByte();
    if (type != kCompound) {
        if (type != kEnd) {
            skipPayload(reader, type, 0);
        }
        return;
    }
    walkCompound(reader, onKey, std::string{}, 1);
}

void skip(ByteReader& reader) {
    const std::int32_t type = reader.readByte();
    if (type != kEnd) {
        skipPayload(reader, type, 0);
    }
}

} // namespace basalt::swarm::nbt
