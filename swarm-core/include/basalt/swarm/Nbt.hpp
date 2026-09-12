#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace basalt::swarm {

class ByteReader;

/// Enough NBT to walk past a tag without understanding it.
///
/// A registry element is a tag, and a bot has to get past one to read the name of the next entry.
/// Nothing here builds a tree: it is a reader that skips, which is all a check of names needs.
namespace nbt {

/// Skips one tag, id and all, as it travels in a packet: no name on the outermost one.
void skip(ByteReader& reader);

/// The same walk, saying what it passes: every member of the outermost compound, and every member
/// of the compounds directly inside it, as `outer.inner`.
///
/// Two levels because that is where a version's shape lives. A list is named but not entered: what
/// is inside one is repetition, not shape.
void walkKeys(ByteReader& reader, const std::function<void(std::string_view path)>& onKey);

/// The words inside a text component, which since 1.20.3 travels as a tag rather than as json. Not a
/// renderer: it collects every `text` and the key of every `translate`, descending into `extra` and
/// `with`. A bot has no language table, so a translated line reads as its key and its fills.
[[nodiscard]] std::string text(ByteReader& reader);

} // namespace nbt

} // namespace basalt::swarm
