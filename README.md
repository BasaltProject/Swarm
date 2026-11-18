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
  <strong>A stress testing and bot swarm tool for Minecraft servers, Java and Bedrock.</strong>
</p>

<p align="center">
  <img alt="Java Edition" src="https://img.shields.io/badge/Java_Edition-26.3-62B47A?logo=minecraft&logoColor=white">
  <img alt="Bedrock Edition" src="https://img.shields.io/badge/Bedrock_Edition-26.60-62B47A?logo=minecraft&logoColor=white">
  <img alt="C++" src="https://img.shields.io/badge/language-C%2B%2B-00599C?logo=cplusplus&logoColor=white">
  <img alt="Platforms" src="https://img.shields.io/badge/platforms-macOS_%7C_Linux_%7C_Windows-00599C">
  <img alt="License" src="https://img.shields.io/badge/license-GPL--3.0-green">
</p>

> ⚠️ **Development use only.** Swarm is for development environments and benchmarking. Never use it against servers
> you do not own or have explicit permission to test.

## About

Swarm connects large swarms of bots to a server to test its performance, concurrency and stability. It speaks both
editions: Java from 1.8 to 26.3, and Bedrock with `--bedrock`. A Java bot claims whatever version `--version` asks
for, so a server that translates older clients can be tested with the same swarm.

Swarm deliberately does **not** use [Wire](https://github.com/BasaltProject/Wire). A bot that speaks through the same
code as the server it tests cannot catch a bug in that code, because both sides would make the same mistake.

## Usage

```bash
BasaltSwarm --ip 127.0.0.1:25565 --count 500 --physics        # 500 Java bots with physics
BasaltSwarm --count 16 --spread 2048 --physics                 # spread over a grid
BasaltSwarm --count 50 --version 26.1                          # claim an older version
BasaltSwarm --bedrock --count 20                               # Bedrock bots
```

Bots can also run commands, chat, place and break blocks, or follow a script. `BasaltSwarm --help` lists every option.

## Building

```bash
git clone https://github.com/BasaltProject/Swarm.git
cd Swarm
cmake --preset release
cmake --build build/release
```

Needs CMake 3.28+, Ninja and Clang. The binary ends up in `build/release/bin/`.

## License

[GPL-3.0](LICENSE).

Basalt is not affiliated with Mojang Studios or Microsoft. Minecraft is a trademark of Mojang Studios.
