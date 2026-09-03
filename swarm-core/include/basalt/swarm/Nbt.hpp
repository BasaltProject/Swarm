#pragma once

namespace basalt::swarm {

class ByteReader;

/// Enough NBT to walk past a tag without understanding it.
///
/// A registry element is a tag, and a bot has to get past one to read the name of the next entry.
/// Nothing here builds a tree: it is a reader that skips, which is all a check of names needs.
namespace nbt {

/// Skips one tag, id and all, as it travels in a packet: no name on the outermost one.
void skip(ByteReader& reader);

} // namespace nbt

} // namespace basalt::swarm
