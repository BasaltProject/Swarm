package org.basaltmc.swarm

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class SpreadTest {
    @Test
    fun gridIsCentredOnTheSpawn() {
        val grid = spawnGrid(4, 1000.0)
        assertEquals(
            listOf(
                SpawnTarget(-500.0, -500.0),
                SpawnTarget(500.0, -500.0),
                SpawnTarget(-500.0, 500.0),
                SpawnTarget(500.0, 500.0),
            ),
            grid,
        )
    }

    @Test
    fun neighboursSitOneSpacingApart() {
        val grid = spawnGrid(9, 2048.0)
        assertEquals(9, grid.size)
        assertEquals(2048.0, grid[1].x - grid[0].x)
        assertEquals(2048.0, grid[3].z - grid[0].z)
    }

    @Test
    fun aPartialRowStillFits() {
        val grid = spawnGrid(5, 256.0)
        assertEquals(5, grid.size)
        assertTrue(grid.distinct().size == 5)
    }

    @Test
    fun noSpacingMeansNoGrid() {
        assertEquals(emptyList(), spawnGrid(10, 0.0))
        assertEquals(emptyList(), spawnGrid(0, 256.0))
    }
}
