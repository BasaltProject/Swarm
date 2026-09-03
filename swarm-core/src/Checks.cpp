#include <basalt/swarm/Bytes.hpp>
#include <basalt/swarm/Checks.hpp>
#include <basalt/swarm/KnownVersions.hpp>
#include <basalt/swarm/Nbt.hpp>

namespace basalt::swarm {
namespace {

constexpr std::int32_t kKind = 3;
constexpr std::int32_t kRoot = 0;
constexpr std::int32_t kArgument = 2;
constexpr std::int32_t kRedirect = 8;
constexpr std::int32_t kCustomSuggestions = 16;

/// parity: `ArgumentTypeInfos`' own registration order, which has not moved since 1.19.
constexpr std::int32_t kBrigadierDouble = 1;
constexpr std::int32_t kBrigadierFloat = 2;
constexpr std::int32_t kBrigadierInteger = 3;
constexpr std::int32_t kBrigadierLong = 4;
constexpr std::int32_t kBrigadierString = 5;
constexpr std::int32_t kEntity = 6;
constexpr std::int32_t kScoreHolder = 29;
constexpr std::int32_t kTime = 41;
constexpr std::int32_t kResourceOrTag = 44;
constexpr std::int32_t kResourceOrTagKey = 45;
constexpr std::int32_t kResource = 46;
constexpr std::int32_t kResourceKey = 47;
constexpr std::int32_t kResourceSelector = 48;

/// A packet claiming more entries than the bytes left can hold is malformed, and looping on its
/// number is how a bot hangs instead of reporting it.
[[nodiscard]] bool sane(const ByteReader& data, const std::int32_t count) {
    return !data.failed() && count >= 0 && static_cast<std::size_t>(count) <= data.remaining();
}

/// Walks past a parser's own configuration, answering the registry it names when it names one.
[[nodiscard]] std::string readParserProperties(ByteReader& data, const std::int32_t parser) {
    switch (parser) {
    case kBrigadierFloat:
    case kBrigadierDouble: {
        const std::int32_t flags = data.readByte();
        const std::size_t width = parser == kBrigadierDouble ? 8 : 4;
        if ((flags & 1) != 0) {
            data.skip(width);
        }
        if ((flags & 2) != 0) {
            data.skip(width);
        }
        return {};
    }
    case kBrigadierInteger:
    case kBrigadierLong: {
        const std::int32_t flags = data.readByte();
        const std::size_t width = parser == kBrigadierLong ? 8 : 4;
        if ((flags & 1) != 0) {
            data.skip(width);
        }
        if ((flags & 2) != 0) {
            data.skip(width);
        }
        return {};
    }
    case kBrigadierString:
    case kEntity:
    case kScoreHolder:
        static_cast<void>(data.readByte());
        return {};
    case kTime:
        data.skip(4);
        return {};
    case kResource:
    case kResourceKey:
    case kResourceOrTag:
    case kResourceOrTagKey:
    case kResourceSelector:
        return data.readVarString();
    default:
        return {};
    }
}

} // namespace

bool Checks::checkable() const { return knownVersions::isChecked(protocol_); }

std::int32_t Checks::packetId(const std::string_view packet) const {
    return knownVersions::packetId(protocol_, packet);
}

void Checks::registryData(ByteReader& data) {
    const std::string registry = data.readVarString();
    if (!knownVersions::knows(protocol_, registry)) {
        complain("registry " + registry + " does not exist in this version, and being sent it is fatal");
    }
    const std::int32_t entries = data.readVarInt();
    for (std::int32_t i = 0; i < entries && sane(data, 0); ++i) {
        const std::string entry = data.readVarString();
        if (!knownVersions::hasEntry(protocol_, registry, entry)) {
            complain(registry + " has no " + entry + " in this version, and its element will not parse");
        }
        if (data.readByte() != 0) {
            nbt::skip(data);
        }
    }
}

void Checks::tags(ByteReader& data) {
    const std::int32_t groups = data.readVarInt();
    for (std::int32_t g = 0; g < groups && sane(data, 0); ++g) {
        const std::string registry = data.readVarString();
        if (!knownVersions::knows(protocol_, registry)) {
            complain("tags name the registry " + registry + ", which does not exist here");
        }
        const std::int32_t size = knownVersions::size(protocol_, registry);
        const std::int32_t tags = data.readVarInt();
        for (std::int32_t t = 0; t < tags && sane(data, 0); ++t) {
            static_cast<void>(data.readVarString());
            const std::int32_t ids = data.readVarInt();
            for (std::int32_t i = 0; i < ids && sane(data, 0); ++i) {
                const std::int32_t id = data.readVarInt();
                if (size >= 0 && id >= size) {
                    complain("a tag of " + registry + " points at " + std::to_string(id) + ", and that registry ends at " +
                             std::to_string(size - 1));
                }
            }
        }
    }
}

void Checks::commands(ByteReader& data) {
    const std::int32_t nodes = data.readVarInt();
    for (std::int32_t n = 0; n < nodes && sane(data, 0); ++n) {
        const std::int32_t flags = data.readByte();
        const std::int32_t children = data.readVarInt();
        for (std::int32_t c = 0; c < children && sane(data, 0); ++c) {
            static_cast<void>(data.readVarInt());
        }
        if ((flags & kRedirect) != 0) {
            static_cast<void>(data.readVarInt());
        }
        const std::int32_t kind = flags & kKind;
        if (kind != kRoot) {
            static_cast<void>(data.readVarString());
        }
        if (kind == kArgument) {
            const std::int32_t parser = data.readVarInt();
            const std::string registry = readParserProperties(data, parser);
            if (!registry.empty() && !knownVersions::knows(protocol_, registry)) {
                complain("a command argument names the registry " + registry + ", which does not exist here");
            }
            if ((flags & kCustomSuggestions) != 0) {
                static_cast<void>(data.readVarString());
            }
        }
    }
}

void Checks::updateRecipes(ByteReader& data) {
    const std::int32_t groups = data.readVarInt();
    for (std::int32_t g = 0; g < groups && sane(data, 0); ++g) {
        static_cast<void>(data.readVarString());
        const std::int32_t ids = data.readVarInt();
        for (std::int32_t i = 0; i < ids && sane(data, 0); ++i) {
            static_cast<void>(data.readVarInt());
        }
    }
    const std::int32_t items = knownVersions::size(protocol_, "minecraft:item");
    const std::int32_t displays = knownVersions::size(protocol_, "minecraft:slot_display");
    const std::int32_t options = data.readVarInt();
    for (std::int32_t o = 0; o < options && sane(data, 0); ++o) {
        const std::int32_t inputs = data.readVarInt();
        for (std::int32_t i = 0; i < inputs - 1 && sane(data, 0); ++i) {
            checkId(data.readVarInt(), items, "minecraft:item");
        }
        checkId(data.readVarInt(), displays, "minecraft:slot_display");
        checkId(data.readVarInt(), items, "minecraft:item");
        static_cast<void>(data.readVarInt());
        static_cast<void>(data.readVarInt());
        static_cast<void>(data.readVarInt());
    }
}

void Checks::checkId(const std::int32_t id, const std::int32_t size, const std::string_view registry) {
    if (size >= 0 && id >= size) {
        complain(std::string{registry} + " has " + std::to_string(size) + " entries here and the server sent " +
                 std::to_string(id));
    }
}

void Checks::complain(const std::string& what) {
    if (!complained_.insert(what).second) {
        return;
    }
    problems_++;
    if (report_) {
        report_(what);
    }
}

} // namespace basalt::swarm
