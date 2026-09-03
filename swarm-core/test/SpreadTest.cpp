#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <basalt/swarm/Spread.hpp>

#include <set>

using basalt::swarm::SpawnTarget;
using basalt::swarm::spawnGrid;

TEST_CASE("the grid is centred on the spawn") {
    const std::vector<SpawnTarget> grid = spawnGrid(4, 1000.0);
    REQUIRE(grid.size() == 4);
    CHECK(grid[0] == SpawnTarget{.x = -500.0, .z = -500.0});
    CHECK(grid[1] == SpawnTarget{.x = 500.0, .z = -500.0});
    CHECK(grid[2] == SpawnTarget{.x = -500.0, .z = 500.0});
    CHECK(grid[3] == SpawnTarget{.x = 500.0, .z = 500.0});
}

TEST_CASE("neighbours sit one spacing apart") {
    const std::vector<SpawnTarget> grid = spawnGrid(9, 2048.0);
    REQUIRE(grid.size() == 9);
    CHECK(grid[1].x - grid[0].x == 2048.0);
    CHECK(grid[3].z - grid[0].z == 2048.0);
}

TEST_CASE("a partial row still fits, and nobody shares a spot") {
    const std::vector<SpawnTarget> grid = spawnGrid(5, 256.0);
    REQUIRE(grid.size() == 5);
    std::set<std::pair<double, double>> distinct;
    for (const SpawnTarget& target : grid) {
        distinct.emplace(target.x, target.z);
    }
    CHECK(distinct.size() == 5);
}

TEST_CASE("no spacing means no grid") {
    CHECK(spawnGrid(10, 0.0).empty());
    CHECK(spawnGrid(0, 256.0).empty());
}
