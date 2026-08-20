package org.basaltmc.swarm

import kotlin.math.ceil
import kotlin.math.sqrt

/** Where a bot walks to once the server has told it where it spawned. */
data class SpawnTarget(val x: Double, val z: Double)

/**
 * A square grid of [count] positions [spacing] blocks apart, centred on the spawn.
 *
 * Bots left at the spawn all sit in the same corner of the same world, so a run measures one busy
 * area and nothing else. Spreading them is what puts real distance between them.
 */
fun spawnGrid(count: Int, spacing: Double): List<SpawnTarget> {
    if (count <= 0 || spacing <= 0.0) return emptyList()
    val side = ceil(sqrt(count.toDouble())).toInt()
    val offset = (side - 1) / 2.0
    return List(count) { index ->
        SpawnTarget((index % side - offset) * spacing, (index / side - offset) * spacing)
    }
}
