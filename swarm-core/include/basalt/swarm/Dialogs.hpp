#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace basalt::swarm {

class ByteReader;

namespace dialogs {

struct Click {
    std::string id;
    std::vector<std::uint8_t> payload;
    std::string label;
};

/// Reads an inline `ShowDialog` body and answers it the way the game does: the
/// button's own additions plus the initial value of every input. Null for a
/// dialog by registry id, one without that button, or a shape it cannot read.
[[nodiscard]] std::optional<Click> answer(ByteReader& body, std::int32_t button);

} // namespace dialogs

} // namespace basalt::swarm
