package org.basaltmc.swarm

import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking

fun main(args: Array<String>) = runBlocking {
    println("Basalt Swarm starting...")

    var host = "127.0.0.1"
    var port = 25565
    var count = 1
    var delayMs = 200L
    var physicsEnabled = false
    var spread = 0.0
    var profile = Protocols.newest

    var i = 0
    while (i < args.size) {
        when (args[i]) {
            "--ip" -> {
                val ipArg = args.getOrNull(i + 1) ?: host
                if (":" in ipArg) {
                    host = ipArg.substringBefore(":")
                    port = ipArg.substringAfter(":").toIntOrNull() ?: port
                } else {
                    host = ipArg
                }
                i += 2
            }
            "--count", "-c" -> {
                count = args.getOrNull(i + 1)?.toIntOrNull() ?: count
                i += 2
            }
            "--delay", "-d" -> {
                delayMs = args.getOrNull(i + 1)?.toLongOrNull() ?: delayMs
                i += 2
            }
            "--physics" -> {
                physicsEnabled = true
                i++
            }
            "--no-physics" -> {
                physicsEnabled = false
                i++
            }
            "--version", "--protocol" -> {
                val asked = args.getOrNull(i + 1)
                val found = asked?.let { Protocols.find(it) }
                val number = asked?.toIntOrNull()
                when {
                    found != null -> profile = found
                    // A bare number this tool has no ids for is still worth claiming: it is how a
                    // server's answer to an unknown version gets tested.
                    number != null -> {
                        profile = Protocols.pretend(number)
                        println("No packet ids for protocol $number: past the handshake this speaks 26.2.")
                    }
                    else -> {
                        println("Unknown version: $asked")
                        println("Known: " + Protocols.supported.joinToString(", ") { "${it.label} (${it.protocol})" })
                        return@runBlocking
                    }
                }
                i += 2
            }
            "--spread" -> {
                spread = args.getOrNull(i + 1)?.toDoubleOrNull() ?: spread
                i += 2
            }
            else -> i++
        }
    }

    val layout = if (spread > 0.0) "${spread.toInt()} blocks apart" else "on the spawn"
    println(
        "Target: $host:$port | Bots: $count | Delay: ${delayMs}ms | Physics: $physicsEnabled | " +
            "Layout: $layout | Speaking: ${profile.label} (${profile.protocol})",
    )

    val targets = spawnGrid(count, spread)
    for (id in 1..count) {
        launch {
            val bot = Bot(id, host, port, physicsEnabled, targets.getOrNull(id - 1), profile)
            bot.connect()
        }
        delay(delayMs)
    }
}
