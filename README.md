# Journeyman Game Engine

This engine is for educational purposes and not meant to be a _real_ game engine. I hope you like it, and I hope you can learn something from it!

## @TODO

- Integrate `memory`
  - `ThreadArenaRegistry` --> `ThreadPool`
    - ThreadPool Owns the Arenas
    - Setup + Register Per-Thread Arena
    - Frame Reset Integration
    - TLS Access Everywhere

## Requirements

- CMake <= 3.20 && >= 4.0.0
- Go 1.24+
- Docker (instead of nodejs, for compiling the scripts)

If you have CMake version >= 4.0.0:

```bash
cmake -B build -S . -DCMAKE_POLICY_VERSION_MINIMUM=3.5
```

Some of the wasm3 packages require an older version of cmake.

Tested on Go 1.24

## Build

### Engine

#### Building the Binary

```bash
mkdir build
cmake -B build -S .

```

### CLI

#### Installation

```bash
cd cli
go install ./cmd/jm
```

#### Building the binary

```bash
cd cli
go build -o jm ./cmd/jm
```

## Building and Running the Demo Game

The demo game needs to be built in order to run it:

1. Install the CLI (detailed above)
2. Build the game engine (detailed above)
3. Make sure the `.jm.json` file's `engine` field correctly points to the engine binary built in step 2
4. Build the game files

```bash
cd demo_game
jm build
jm run build
```
