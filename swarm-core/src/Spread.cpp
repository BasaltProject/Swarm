#include <basalt/swarm/Spread.hpp>

#include <cmath>

namespace basalt::swarm {

std::vector<SpawnTarget> spawnGrid(const std::int32_t count, const double spacing) {
    if (count <= 0 || spacing <= 0.0) {
        return {};
    }
    const auto side = static_cast<std::int32_t>(std::ceil(std::sqrt(static_cast<double>(count))));
    const double offset = (side - 1) / 2.0;
    std::vector<SpawnTarget> grid;
    grid.reserve(static_cast<std::size_t>(count));
    for (std::int32_t index = 0; index < count; ++index) {
        grid.push_back(SpawnTarget{.x = ((index % side) - offset) * spacing,
                                   .z = ((index / side) - offset) * spacing});
    }
    return grid;
}

} // namespace basalt::swarm
