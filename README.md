<p align="center">
  <img src="docs/assets/basalt-logo.png" alt="Basalt" width="360">
</p>

<h2 align="center">Basalt Swarm</h2>

<p align="center">
  <strong>A high-performance stress testing and bot swarm tool for Minecraft servers.</strong>
</p>

<p align="center">
  <img alt="Minecraft" src="https://img.shields.io/badge/Minecraft-26.2-62B47A?logo=minecraft&logoColor=white">
  <img alt="Protocol" src="https://img.shields.io/badge/protocol-776-62B47A">
  <img alt="Kotlin" src="https://img.shields.io/badge/Kotlin-2.4.10-7F52FF?logo=kotlin&logoColor=white">
  <img alt="Kotlin/Native" src="https://img.shields.io/badge/Kotlin%2FNative-macOS_%7C_Linux_%7C_Windows-7F52FF?logo=kotlin&logoColor=white">
  <img alt="Build" src="https://img.shields.io/badge/build-Gradle-02303A?logo=gradle&logoColor=white">
  <img alt="License" src="https://img.shields.io/badge/license-GPL--3.0-green">
</p>

> ⚠️ **Development Use Only.** Basalt Swarm is a stress testing tool designed strictly for development environments and benchmarking. It must not be used maliciously or against servers you do not own or have explicit permission to test.

## Overview

Basalt Swarm is part of the Basalt Project ecosystem. It is a dedicated tool for connecting large bot swarms to test the performance, concurrency, and stability of Minecraft servers.

Like the rest of the Basalt Project, it is written entirely in **Kotlin/Native** and distributed as a native binary, requiring no JVM to run. It speaks the Java Edition protocol and is designed to simulate realistic client loads with minimal overhead.

## Building

Since Basalt Swarm is built with Kotlin Multiplatform (targeting Native), you can build the executable for your current operating system using Gradle:

```bash
git clone https://github.com/BasaltProject/Swarm.git
cd Swarm
./gradlew assemble
```

The resulting binaries will be placed in `swarm-core/build/bin/`. Depending on your OS and the build type, you can find the executable at:
- macOS (Apple Silicon): `swarm-core/build/bin/macosArm64/basalt-swarmReleaseExecutable/basalt-swarm.kexe`
- Linux (x64): `swarm-core/build/bin/linuxX64/basalt-swarmReleaseExecutable/basalt-swarm.kexe`
- Windows (x64): `swarm-core/build/bin/mingwX64/basalt-swarmReleaseExecutable/basalt-swarm.exe`

*Note: You can also find `basalt-swarmDebugExecutable` for debug builds.*

## Usage

You can run the generated native binary directly from your terminal. Swarm provides several CLI arguments to customize the stress test behavior:

```bash
./basalt-swarm.kexe [options]
```

### Available Arguments:
- `--ip <host:port>` or `<host>` (default: `127.0.0.1:25565`): Target server address and port.
- `--count`, `-c <number>` (default: `1`): Number of bots to spawn.
- `--delay`, `-d <ms>` (default: `200`): Delay in milliseconds between each bot connection.
- `--physics` (default: disabled): Enables the physics engine. Bots will simulate realistic Minecraft gravity, occasionally jump, and wander randomly around the X/Z plane while continuously sending position and rotation packets to the server.
- `--no-physics`: Disables the physics engine. Bots will remain static.
- `--spread <blocks>` (default: disabled): Spreads the bots over a square grid centred on the spawn, that many blocks between neighbours. Without it every bot stands on the spawn, which only ever loads one area of the world. Each bot claims its cell again after every position sync the server sends.

### Examples

**Connect 500 bots locally:**
```bash
./basalt-swarm.kexe --count 500
```

**Connect 100 bots to a specific server with physics enabled:**
```bash
./basalt-swarm.kexe --ip 192.168.1.50:25565 --count 100 --physics
```

**Connect 1000 bots with a very fast 10ms join delay:**
```bash
./basalt-swarm.kexe --ip 127.0.0.1 -c 1000 -d 10 --physics
```

**Spread 16 bots 2048 blocks apart:**
```bash
./basalt-swarm.kexe --count 16 --spread 2048 --physics
```

## License

Basalt Swarm is distributed under the [GNU General Public License v3.0](LICENSE).

Basalt is not affiliated with Mojang Studios or Microsoft. Minecraft is a trademark of Mojang Studios.