#include <basalt/swarm/BlockWatch.hpp>

#include <algorithm>

namespace basalt::swarm {
namespace {

/// The directly stored palette of blocks has no table in front of it: any width past this is a global id.
constexpr std::int32_t kMaxIndirectBlockBits = 8;
constexpr std::int32_t kMaxIndirectBiomeBits = 3;

[[nodiscard]] std::size_t longsFor(const std::int32_t bits, const std::size_t count) {
    const std::size_t perLong = static_cast<std::size_t>(64 / bits);
    return (count + perLong - 1) / perLong;
}

/// Reads one paletted container of `count` entries into `out` (when given) and says whether it parsed.
[[nodiscard]] bool container(ByteReader& body, const std::size_t count, const std::int32_t maxIndirect,
                             std::vector<std::int32_t>* out) {
    const std::int32_t bits = body.readByte();
    if (bits == 0) {
        const std::int32_t value = body.readVarInt();
        if (out != nullptr) {
            out->assign(count, value);
        }
        return !body.failed();
    }
    std::vector<std::int32_t> palette;
    if (bits <= maxIndirect) {
        const std::int32_t size = body.readVarInt();
        for (std::int32_t i = 0; i < size && !body.failed(); ++i) {
            palette.push_back(body.readVarInt());
        }
    }
    const std::size_t words = longsFor(bits, count);
    std::vector<std::uint64_t> data;
    data.reserve(words);
    for (std::size_t i = 0; i < words && !body.failed(); ++i) {
        data.push_back(static_cast<std::uint64_t>(body.readLong()));
    }
    if (body.failed()) {
        return false;
    }
    if (out != nullptr) {
        out->assign(count, 0);
        const std::size_t perLong = static_cast<std::size_t>(64 / bits);
        const std::uint64_t mask = (std::uint64_t{1} << bits) - 1;
        for (std::size_t i = 0; i < count; ++i) {
            const std::uint64_t raw = (data[i / perLong] >> ((i % perLong) * static_cast<std::size_t>(bits))) & mask;
            (*out)[i] = palette.empty() ? static_cast<std::int32_t>(raw)
                                        : (raw < palette.size() ? palette[static_cast<std::size_t>(raw)] : 0);
        }
    }
    return true;
}

} // namespace

BlockWatch::BlockWatch(const std::array<std::int32_t, 3> low, const std::array<std::int32_t, 3> high)
    : low_(low), high_(high) {
    const std::size_t volume = static_cast<std::size_t>(high[0] - low[0] + 1) *
                               static_cast<std::size_t>(high[1] - low[1] + 1) *
                               static_cast<std::size_t>(high[2] - low[2] + 1);
    states_.assign(volume, -1);
}

bool BlockWatch::inside(const std::int32_t x, const std::int32_t y, const std::int32_t z) const {
    return x >= low_[0] && x <= high_[0] && y >= low_[1] && y <= high_[1] && z >= low_[2] && z <= high_[2];
}

void BlockWatch::put(const std::int32_t x, const std::int32_t y, const std::int32_t z, const std::int32_t state) {
    const std::size_t width = static_cast<std::size_t>(high_[0] - low_[0] + 1);
    const std::size_t depth = static_cast<std::size_t>(high_[2] - low_[2] + 1);
    states_[(static_cast<std::size_t>(y - low_[1]) * depth + static_cast<std::size_t>(z - low_[2])) * width +
            static_cast<std::size_t>(x - low_[0])] = state;
}

std::int32_t BlockWatch::stateAt(const std::int32_t x, const std::int32_t y, const std::int32_t z) const {
    if (!inside(x, y, z)) {
        return -1;
    }
    const std::size_t width = static_cast<std::size_t>(high_[0] - low_[0] + 1);
    const std::size_t depth = static_cast<std::size_t>(high_[2] - low_[2] + 1);
    return states_[(static_cast<std::size_t>(y - low_[1]) * depth + static_cast<std::size_t>(z - low_[2])) * width +
                   static_cast<std::size_t>(x - low_[0])];
}

bool BlockWatch::chunk(ByteReader& body) {
    const std::int32_t chunkX = body.readInt();
    const std::int32_t chunkZ = body.readInt();
    const std::int32_t heightmaps = body.readVarInt();
    for (std::int32_t i = 0; i < heightmaps && !body.failed(); ++i) {
        static_cast<void>(body.readVarInt());
        const std::int32_t longs = body.readVarInt();
        body.skip(static_cast<std::size_t>(std::max(longs, 0)) * 8);
    }
    const std::int32_t size = body.readVarInt();
    if (body.failed() || size < 0) {
        return false;
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    for (std::uint8_t& byte : data) {
        byte = body.readByte();
    }
    if (body.failed()) {
        return false;
    }
    if ((chunkX << 4) + 15 < low_[0] || (chunkX << 4) > high_[0] || (chunkZ << 4) + 15 < low_[2] ||
        (chunkZ << 4) > high_[2]) {
        return false;
    }
    // The overworld has 24 sections from -64; the others 16 from 0, which is all this follows.
    ByteReader sections{data};
    std::vector<std::vector<std::int32_t>> parsed;
    while (sections.remaining() > 0) {
        static_cast<void>(sections.readShort());
        static_cast<void>(sections.readShort());
        std::vector<std::int32_t> blocks;
        if (!container(sections, 4096, kMaxIndirectBlockBits, &blocks) ||
            !container(sections, 64, kMaxIndirectBiomeBits, nullptr)) {
            break;
        }
        parsed.push_back(std::move(blocks));
    }
    const std::int32_t minSection = parsed.size() == 24 ? -4 : 0;
    bool touched = false;
    for (std::size_t section = 0; section < parsed.size(); ++section) {
        const std::int32_t baseY = (minSection + static_cast<std::int32_t>(section)) * 16;
        for (std::size_t index = 0; index < 4096; ++index) {
            const std::int32_t x = (chunkX << 4) + static_cast<std::int32_t>(index & 15);
            const std::int32_t z = (chunkZ << 4) + static_cast<std::int32_t>((index >> 4) & 15);
            const std::int32_t y = baseY + static_cast<std::int32_t>(index >> 8);
            if (inside(x, y, z)) {
                put(x, y, z, parsed[section][index]);
                touched = true;
            }
        }
    }
    return touched;
}

bool BlockWatch::blockUpdate(ByteReader& body) {
    const std::int64_t packed = body.readLong();
    const std::int32_t state = body.readVarInt();
    if (body.failed()) {
        return false;
    }
    const std::int32_t x = static_cast<std::int32_t>(packed >> 38);
    const std::int32_t y =
        static_cast<std::int32_t>(static_cast<std::int64_t>(static_cast<std::uint64_t>(packed) << 52) >> 52);
    const std::int32_t z =
        static_cast<std::int32_t>(static_cast<std::int64_t>(static_cast<std::uint64_t>(packed) << 26) >> 38);
    if (!inside(x, y, z)) {
        return false;
    }
    put(x, y, z, state);
    return true;
}

bool BlockWatch::sectionUpdate(ByteReader& body) {
    const auto packed = static_cast<std::uint64_t>(body.readLong());
    const std::int32_t sectionX = static_cast<std::int32_t>(static_cast<std::int64_t>(packed) >> 42);
    const std::int32_t sectionY = static_cast<std::int32_t>(static_cast<std::int64_t>(packed << 44) >> 44);
    const std::int32_t sectionZ = static_cast<std::int32_t>(static_cast<std::int64_t>(packed << 22) >> 42);
    const std::int32_t count = body.readVarInt();
    bool touched = false;
    for (std::int32_t i = 0; i < count && !body.failed(); ++i) {
        // A varlong whose low twelve bits are the position and the rest the state.
        std::uint64_t value = 0;
        for (int shift = 0; shift < 70; shift += 7) {
            const std::uint8_t byte = body.readByte();
            value |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0 || body.failed()) {
                break;
            }
        }
        const std::int32_t state = static_cast<std::int32_t>(value >> 12);
        const std::int32_t local = static_cast<std::int32_t>(value & 0xFFF);
        const std::int32_t x = (sectionX << 4) + ((local >> 8) & 15);
        const std::int32_t z = (sectionZ << 4) + ((local >> 4) & 15);
        const std::int32_t y = (sectionY << 4) + (local & 15);
        if (inside(x, y, z)) {
            put(x, y, z, state);
            touched = true;
        }
    }
    return touched;
}

std::string BlockWatch::summary() const {
    std::map<std::int32_t, std::size_t> counts;
    std::size_t known = 0;
    for (const std::int32_t state : states_) {
        if (state >= 0) {
            ++known;
            ++counts[state];
        }
    }
    std::string text = std::to_string(known) + " of " + std::to_string(states_.size()) + " known:";
    std::size_t listed = 0;
    for (const auto& [state, count] : counts) {
        if (listed++ == 8) {
            text += " ...";
            break;
        }
        text += " state " + std::to_string(state) + " x" + std::to_string(count);
    }
    return text;
}

} // namespace basalt::swarm
