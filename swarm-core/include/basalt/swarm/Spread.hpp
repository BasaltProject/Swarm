#pragma once

#include <cstdint>
#include <vector>

namespace basalt::swarm {

/// Where a bot walks to once the server has told it where it spawned.
struct SpawnTarget {
    double x = 0.0;
    double z = 0.0;

    friend bool operator==(const SpawnTarget&, const SpawnTarget&) = default;
};

/// A square grid of `count` positions `spacing` blocks apart, centred on the spawn.
///
/// Bots left at the spawn all sit in the same corner of the same world, so a run measures one busy
/// area and nothing else. Spreading them is what puts real distance between them.
[[nodiscard]] std::vector<SpawnTarget> spawnGrid(std::int32_t count, double spacing);

} // namespace basalt::swarm
