#pragma once

#include <basalt/swarm/Bytes.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace basalt::swarm {

/// Follows a box of blocks through the chunk, block and section packets of protocol 777.
class BlockWatch {
public:
    BlockWatch(std::array<std::int32_t, 3> low, std::array<std::int32_t, 3> high);

    /// Each returns whether the box was touched.
    bool chunk(ByteReader& body);

    bool blockUpdate(ByteReader& body);

    bool sectionUpdate(ByteReader& body);

    /// How many blocks of the box are known and how many carry each state id.
    [[nodiscard]] std::string summary() const;

    [[nodiscard]] std::int32_t stateAt(std::int32_t x, std::int32_t y, std::int32_t z) const;

private:
    [[nodiscard]] bool inside(std::int32_t x, std::int32_t y, std::int32_t z) const;

    void put(std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t state);

    std::array<std::int32_t, 3> low_;
    std::array<std::int32_t, 3> high_;
    std::vector<std::int32_t> states_;
};

} // namespace basalt::swarm
