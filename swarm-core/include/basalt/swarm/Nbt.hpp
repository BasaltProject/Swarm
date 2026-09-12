#pragma once

#include <functional>
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

} // namespace nbt

} // namespace basalt::swarm
