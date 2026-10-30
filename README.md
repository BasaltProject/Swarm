<p align="center">
  <a href="https://raw.githubusercontent.com/BasaltProject/.github/main/assets/basalt-logo-white.svg#gh-dark-mode-only">
    <img src="https://raw.githubusercontent.com/BasaltProject/.github/main/assets/basalt-logo-white.svg" alt="Basalt" width="360">
  </a>
  <a href="https://raw.githubusercontent.com/BasaltProject/.github/main/assets/basalt-logo-black.svg#gh-light-mode-only">
    <img src="https://raw.githubusercontent.com/BasaltProject/.github/main/assets/basalt-logo-black.svg" alt="Basalt" width="360">
  </a>
</p>

<h2 align="center">Basalt Swarm</h2>

<p align="center">
  <strong>A high-performance stress testing and bot swarm tool for Minecraft servers.</strong>
</p>

<p align="center">
  <img alt="Minecraft" src="https://img.shields.io/badge/Minecraft-26.3-62B47A?logo=minecraft&logoColor=white">
  <img alt="Protocol" src="https://img.shields.io/badge/protocol-777-62B47A">
  <img alt="C++" src="https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white">
  <img alt="Platforms" src="https://img.shields.io/badge/platforms-macOS_%7C_Linux_%7C_Windows-00599C">
  <img alt="Build" src="https://img.shields.io/badge/build-CMake-064F8C?logo=cmake&logoColor=white">
  <img alt="License" src="https://img.shields.io/badge/license-GPL--3.0-green">
</p>

> ⚠️ **Development Use Only.** Basalt Swarm is a stress testing tool designed strictly for development environments and benchmarking. It must not be used maliciously or against servers you do not own or have explicit permission to test.

## Overview

Basalt Swarm is part of the Basalt Project ecosystem. It is a dedicated tool for connecting large bot swarms to test the performance, concurrency, and stability of Minecraft servers.

Like the rest of the Basalt Project, it is written entirely in **C++23** and distributed as a native binary, requiring no JVM to run. It speaks the Java Edition protocol and is designed to simulate realistic client loads with minimal overhead.

It speaks **both editions**: Java from 1.8 to 26.3, and Bedrock with `--bedrock`.

A Java bot claims whatever version `--version` asks for, which is how a server that translates older clients gets
tested: the same swarm can arrive as 26.3, as 26.2, or as a number nobody has released yet. Version support is defined
by `ProtocolProfile` in the source tree and listed in [docs/versions.md](docs/versions.md).

## Building

You need CMake 3.28 or newer, Ninja and Clang. Nothing else: zlib is vendored and built from source,
and Swarm depends on no other Basalt repository.

```bash
git clone https://github.com/BasaltProject/Swarm.git
cd Swarm
cmake --preset release
cmake --build build/release
```

The binary lands in `build/release/bin/`, named for what it is:

```
build/release/bin/BasaltSwarm-26.3.0-dev-macOS-arm64
```

`cmake --preset debug` builds the debug tree, and `ctest --preset debug` runs the tests.

Swarm deliberately does **not** consume [Wire](https://github.com/BasaltProject/Wire), the protocol
layer the server and the proxy share. A bot that speaks through the same code as the server it is
testing cannot catch a bug in that code: both sides make the same mistake and agree with each other.
Its reader, its framing and its version table are its own for that reason.

## Usage

You can run the generated native binary directly from your terminal. Swarm provides several CLI arguments to customize the stress test behavior:

```bash
./BasaltSwarm-26.3.0-dev-macOS-arm64 [options]
```

### Available Arguments:
- `--ip <host:port>` or `<host>` (default: `127.0.0.1:25565`): Target server address and port.
- `--count`, `-c <number>` (default: `1`): Number of bots to spawn.
- `--delay`, `-d <ms>` (default: `200`): Delay in milliseconds between each bot connection.
- `--physics` (default: disabled): Enables the physics engine. Bots will simulate realistic Minecraft gravity, occasionally jump, and wander randomly around the X/Z plane while continuously sending position and rotation packets to the server.
- `--no-physics`: Disables the physics engine. Bots will remain static.
- `--version`, `--protocol <version>` (default: `26.3`): Which client version the bots claim to be. Takes a name (`26.1`) or a protocol number (`775`). A number this tool has no packet ids for is still sent, so a server's answer to a version it does not know can be tested; past the handshake those bots speak 26.3.
- `--bedrock` (default: disabled): The bots arrive as Bedrock clients instead of Java ones: RakNet, the key exchange and the encrypted stream, all the way to spawning. The default port moves to `19132` with the flag, and `--version`, `--physics` and `--spread` do not apply to them yet.
- `--stay <ms>` (default: `5000`): How long a Bedrock bot stays once it has spawned.
- `--answer-dialog <button>` (default: none): A Java bot that is shown a dialog presses that button (from zero) and answers with the initial value of every input, the way the game answers a `dynamic/custom` action. Only for versions whose packet ids this tool knows (26.3 so far).
- `--grounded` (default: disabled): A Bedrock bot says it is standing on the ground, which is what a block that reacts to being walked on waits for.
- `--use <n>` (default: `0`): A Bedrock bot uses what is in its hand on nothing that many times, the way a rod is cast and then reeled in. `--use-every <ms>` (default: `3000`) sets how long apart.
- `--bedrock-protocol <number[,number...]>` (default: the one this build speaks): Which Bedrock protocol the bots claim; several are dealt out one per bot in turn. A server that turns them away for it is doing its job; one that turns away the very protocol it advertises in its own server list is not, and that is reported as a problem.
- `--run <command>` (default: none): A command a bot runs without its slash: a Bedrock bot once it is in, a Java bot right after its first position sync. What the server answers arrives as chat, so `--run list` is a round trip through the server's own dispatcher. On Java, repeat the flag to type several in a row, `--run-every <ms>` (default: `1500`) apart. A Bedrock bot runs only the first command.
- `--say <text>` (default: none): A Bedrock bot says this in chat once it is in.
- `--leave-after <ms>` (default: stay until stopped): A Java bot leaves that long after its first position sync.
- `--watch <x,y,z[,x,y,z]>` (default: none): A Java bot reports the state of one block, or of a box between two corners, as the server shows it.
- `--place <x,y,z>` (default: none): A Java bot places what is in hand on the top face of that square. `--place-count <n>` (default: `1`) stacks that many blocks, and `--place-face <0-5>` (default: `1`, up) picks the face: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east.
- `--interact <entity type>` (default: none): A Java bot right-clicks an entity of that type (the protocol's numbering). `--interact-count <n>` (default: `1`) sets how many clicks and `--interact-every <ms>` (default: `1000`) how long apart, and before the first.
- `--send <id>:<hex bytes>` (default: none): A Java bot sends one raw game packet with that id (decimal or `0x` hex). Repeatable.
- `--dump-commands <path>` (default: none): A Java bot writes the command tree the server sends to that file.
- `--dig <x,y,z>`, `--put <x,y,z>` (default: none): A Bedrock bot breaks the block at that position, or clicks it with what is in hand. `--sneak` makes the bot crouch, so a click places against a block instead of using it, and `--hotbar <slot>` picks the held slot.
- `--swap <a,b[,n]>`, `--stash <a,b,n>`, `--take <a,b,n>` (default: none): A Bedrock bot moves items: `--swap` trades two of its carried slots, `--stash` moves from a carried slot into the open block's slot, and `--take` the other way round. `--into <window>` and `--from <window>` pick the container window when it is not the block's own.
- `--spread <blocks>` (default: disabled): Spreads the bots over a square grid centred on the spawn, that many blocks between neighbours. Without it every bot stands on the spawn, which only ever loads one area of the world. Each bot claims its cell again after every position sync the server sends.

### Examples

**Connect 500 bots locally:**
```bash
./BasaltSwarm-26.3.0-dev-macOS-arm64 --count 500
```

**Connect 100 bots to a specific server with physics enabled:**
```bash
./BasaltSwarm-26.3.0-dev-macOS-arm64 --ip 192.168.1.50:25565 --count 100 --physics
```

**Connect 1000 bots with a very fast 10ms join delay:**
```bash
./BasaltSwarm-26.3.0-dev-macOS-arm64 --ip 127.0.0.1 -c 1000 -d 10 --physics
```

**Spread 16 bots 2048 blocks apart:**
```bash
./BasaltSwarm-26.3.0-dev-macOS-arm64 --count 16 --spread 2048 --physics
```

**Connect 50 bots as 26.1 clients, to exercise a server's translation:**
```bash
./BasaltSwarm-26.3.0-dev-macOS-arm64 --count 50 --version 26.1 --physics
```

**Ask a server what it does with a version nobody has released:**
```bash
./BasaltSwarm-26.3.0-dev-macOS-arm64 --count 1 --protocol 999
```

## License

Basalt Swarm is distributed under the [GNU General Public License v3.0](LICENSE).

Basalt is not affiliated with Mojang Studios or Microsoft. Minecraft is a trademark of Mojang Studios.