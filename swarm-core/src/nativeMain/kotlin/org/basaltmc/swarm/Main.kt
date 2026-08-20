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
            else -> i++
        }
    }

    println("Target: $host:$port | Bots: $count | Delay: ${delayMs}ms | Physics: $physicsEnabled")

    for (id in 1..count) {
        launch {
            val bot = Bot(id, host, port, physicsEnabled)
            bot.connect()
        }
        delay(delayMs)
    }
}
