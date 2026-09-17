#pragma once

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <string_view>

namespace basalt::swarm {

class ByteReader;

/// What a real client would refuse, checked on the packets a bot can read. What breaks a client is
/// not the framing but a registry it does not have or an id past the end of one, and both numbers
/// come from that version's own jar (see `KnownVersions`).
///
/// Nothing throws: a bot reports and carries on, so one connection gives the whole list.
class Checks {
public:
    using Report = std::function<void(const std::string& what)>;

    Checks(std::int32_t protocol, Report report) : protocol_(protocol), report_(std::move(report)) {}

    [[nodiscard]] std::int32_t problems() const noexcept { return problems_; }

    /// Where every command tree received is written, one path per line, for diffing against the
    /// jar's `commands.json`. Empty writes nothing.
    void dumpCommandsTo(std::string path) { commandsDump_ = std::move(path); }

    [[nodiscard]] bool checkable() const;

    [[nodiscard]] std::int32_t packetId(std::string_view packet) const;

    /// One registry's worth of elements.
    ///
    /// The name of the registry and the name of every entry: an entry the version has never heard of
    /// is one it will try to parse against a shape it does not have, and it stops loading its
    /// registries altogether rather than skipping it.
    void registryData(ByteReader& data);

    /// The tags: every group names a registry, and every id in it has to be one of that registry's.
    void tags(ByteReader& data);

    /// The command tree, whose arguments name registries the client looks up as it reads.
    ///
    /// Only the shape is walked: a node's parser properties are skipped by knowing which parsers
    /// have any, which is the same handful every version has had since 1.13.
    void commands(ByteReader& data);

    /// The stonecutter's list: an item and the kind of slot it is drawn as, both registry ids.
    void updateRecipes(ByteReader& data);

private:
    void checkId(std::int32_t id, std::int32_t size, std::string_view registry);

    void complain(const std::string& what);

    std::int32_t protocol_;
    Report report_;
    std::set<std::string, std::less<>> complained_;
    std::int32_t problems_ = 0;
    std::string commandsDump_;
};

} // namespace basalt::swarm
