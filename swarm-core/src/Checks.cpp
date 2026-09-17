#include <basalt/swarm/Bytes.hpp>
#include <basalt/swarm/Checks.hpp>
#include <basalt/swarm/KnownVersions.hpp>
#include <basalt/swarm/Nbt.hpp>

#include <fstream>
#include <vector>

namespace basalt::swarm {
namespace {

constexpr std::int32_t kKind = 3;
constexpr std::int32_t kRoot = 0;
constexpr std::int32_t kArgument = 2;
constexpr std::int32_t kRedirect = 8;
constexpr std::int32_t kCustomSuggestions = 16;

/// Where a version's own parser ids are not known, the ones from 1.19 to 1.20.2, which is where the
/// old fixed table was right.
[[nodiscard]] std::int32_t parserId(const std::int32_t protocol, const std::string_view name,
                                    const std::int32_t fallback) {
    const std::int32_t id = knownVersions::argumentType(protocol, name);
    return id >= 0 ? id : fallback;
}

/// A packet claiming more entries than the bytes left can hold is malformed, and looping on its
/// number is how a bot hangs instead of reporting it.
[[nodiscard]] bool sane(const ByteReader& data, const std::int32_t count) {
    return !data.failed() && count >= 0 && static_cast<std::size_t>(count) <= data.remaining();
}

/// Walks past a parser's own configuration, answering the registry it names when it names one.
[[nodiscard]] std::string readParserProperties(ByteReader& data, const std::int32_t protocol,
                                               const std::int32_t parser) {
    const auto is = [protocol, parser](const std::string_view name, const std::int32_t fallback) {
        return parser == parserId(protocol, name, fallback);
    };
    const bool isFloat = is("brigadier:float", 1);
    const bool isDouble = is("brigadier:double", 2);
    const bool isInteger = is("brigadier:integer", 3);
    const bool isLong = is("brigadier:long", 4);
    if (isFloat || isDouble || isInteger || isLong) {
        const std::int32_t flags = data.readByte();
        const std::size_t width = isDouble || isLong ? 8 : 4;
        if ((flags & 1) != 0) {
            data.skip(width);
        }
        if ((flags & 2) != 0) {
            data.skip(width);
        }
        return {};
    }
    if (is("brigadier:string", 5) || is("minecraft:entity", 6) || is("minecraft:score_holder", 29)) {
        static_cast<void>(data.readByte());
        return {};
    }
    if (is("minecraft:time", 40)) {
        data.skip(4);
        return {};
    }
    if (is("minecraft:resource_or_tag", 41) || is("minecraft:resource_or_tag_key", 42) ||
        is("minecraft:resource", 43) || is("minecraft:resource_key", 44) || is("minecraft:resource_selector", -1)) {
        return data.readVarString();
    }
    return {};
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
            // What the element is made of, not only what it is called. A member this version never
            // had is the other half of the same failure: the client refuses the registry and never
            // finishes joining.
            nbt::walkKeys(data, [this, &registry](const std::string_view path) {
                if (!knownVersions::hasEntryKey(protocol_, registry, path)) {
                    complain("an element of " + registry + " is made of " + std::string{path} +
                             ", which nothing in this version has");
                }
            });
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
                    complain("a tag of " + registry + " points at " + std::to_string(id) +
                             ", and that registry ends at " + std::to_string(size - 1));
                }
            }
        }
    }
}

namespace {

struct TreeNode {
    std::int32_t flags = 0;
    std::vector<std::int32_t> children;
    std::int32_t redirect = -1;
    std::string name;
    std::int32_t parser = -1;
    std::string suggestions;
};

/// One line per node reached from the root: path, parser id (-1 on a literal), executable,
/// redirect path and suggestion provider. Redirects are named, not followed, as `commands.json` does.
void dumpTree(const std::vector<TreeNode>& nodes, const std::int32_t root, const std::string& path) {
    std::vector<std::string> paths(nodes.size());
    std::vector<std::int32_t> order;
    std::vector<std::int32_t> stack{root};
    std::vector<bool> seen(nodes.size(), false);
    while (!stack.empty()) {
        const std::int32_t at = stack.back();
        stack.pop_back();
        if (at < 0 || static_cast<std::size_t>(at) >= nodes.size() || seen[static_cast<std::size_t>(at)]) {
            continue;
        }
        seen[static_cast<std::size_t>(at)] = true;
        order.push_back(at);
        for (const std::int32_t child : nodes[static_cast<std::size_t>(at)].children) {
            if (child >= 0 && static_cast<std::size_t>(child) < nodes.size() &&
                !seen[static_cast<std::size_t>(child)]) {
                const std::string& parent = paths[static_cast<std::size_t>(at)];
                paths[static_cast<std::size_t>(child)] =
                    (parent.empty() ? "" : parent + " ") + nodes[static_cast<std::size_t>(child)].name;
                stack.push_back(child);
            }
        }
    }
    std::ofstream out{path, std::ios::trunc};
    for (const std::int32_t at : order) {
        if (at == root) {
            continue;
        }
        const TreeNode& node = nodes[static_cast<std::size_t>(at)];
        const bool redirects = node.redirect >= 0 && static_cast<std::size_t>(node.redirect) < nodes.size();
        out << paths[static_cast<std::size_t>(at)] << '\t' << node.parser << '\t' << ((node.flags & 4) != 0 ? 1 : 0)
            << '\t' << (redirects ? paths[static_cast<std::size_t>(node.redirect)] : std::string{"-"}) << '\t'
            << (node.suggestions.empty() ? std::string{"-"} : node.suggestions) << '\n';
    }
}

} // namespace

void Checks::commands(ByteReader& data) {
    const std::int32_t nodes = data.readVarInt();
    std::vector<TreeNode> tree;
    for (std::int32_t n = 0; n < nodes && sane(data, 0); ++n) {
        TreeNode node;
        node.flags = data.readByte();
        const std::int32_t children = data.readVarInt();
        for (std::int32_t c = 0; c < children && sane(data, 0); ++c) {
            node.children.push_back(data.readVarInt());
        }
        if ((node.flags & kRedirect) != 0) {
            node.redirect = data.readVarInt();
        }
        const std::int32_t kind = node.flags & kKind;
        if (kind != kRoot) {
            node.name = data.readVarString();
        }
        if (kind == kArgument) {
            node.parser = data.readVarInt();
            std::string registry = readParserProperties(data, protocol_, node.parser);
            // The client reads it as an identifier, so a bare path is in the default namespace.
            if (!registry.empty() && registry.find(':') == std::string::npos) {
                registry = "minecraft:" + registry;
            }
            if (!registry.empty() && !knownVersions::knows(protocol_, registry)) {
                complain("a command argument names the registry " + registry + ", which does not exist here");
            }
            if ((node.flags & kCustomSuggestions) != 0) {
                node.suggestions = data.readVarString();
            }
        }
        if (!commandsDump_.empty()) {
            tree.push_back(std::move(node));
        }
    }
    const std::int32_t root = data.readVarInt();
    if (commandsDump_.empty()) {
        return;
    }
    if (data.failed()) {
        complain("the command tree could not be read to the end, so it was not written");
        return;
    }
    dumpTree(tree, root, commandsDump_);
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
