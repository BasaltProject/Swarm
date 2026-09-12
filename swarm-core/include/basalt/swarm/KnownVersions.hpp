#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace basalt::swarm::knownVersions {

/// One version's worth of one table: the protocol number and a space-separated list.
///
/// Kept as one string rather than a parsed structure because most runs ask about one version and
/// parsing all forty of them to answer that is work nobody wanted. The lookup parses on first use.
struct Entry {
    std::int32_t protocol = 0;
    std::string_view value;
};

/// The generated tables. `tools/known-versions.py` writes them out of the jars themselves.
namespace tables {

[[nodiscard]] std::span<const Entry> registryNames();

[[nodiscard]] std::span<const Entry> registrySizes();

[[nodiscard]] std::span<const Entry> entryNames();

[[nodiscard]] std::span<const Entry> entryKeys();

[[nodiscard]] std::span<const Entry> packetIds();

} // namespace tables

/// Whether anything is known about that version at all.
[[nodiscard]] bool isChecked(std::int32_t protocol);

/// The id that packet has there, or -1 when this tool cannot check that version.
[[nodiscard]] std::int32_t packetId(std::int32_t protocol, std::string_view packet);

[[nodiscard]] bool knows(std::int32_t protocol, std::string_view registry);

/// Whether that version has that entry of that registry.
///
/// Only asked about the registries whose data is kept, and only answered when there is any: a
/// version this tool knows nothing about must not be reported as missing all.
[[nodiscard]] bool hasEntry(std::int32_t protocol, std::string_view registry, std::string_view entry);

/// Whether that version's own entries of that registry are ever made of that member. A member it has
/// never seen is not a field it ignores but a registry it refuses, leaving a client that never
/// finishes joining. Checked one and two levels deep, which is where the shape lives.
[[nodiscard]] bool hasEntryKey(std::int32_t protocol, std::string_view registry, std::string_view member);

/// How many entries a registry has there, or -1 when that is not known.
[[nodiscard]] std::int32_t size(std::int32_t protocol, std::string_view registry);

} // namespace basalt::swarm::knownVersions
