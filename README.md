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
- Node.js >= 20 and npm (for compiling AssemblyScript scripts via `npx asc`)
- Python 3.12+ for creating the glad header file

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
4. Install script dependencies (one-time, after a fresh clone):

```bash
cd demo_game/assets/scripts
npm install
```

5. Build the game files:

```bash
cd demo_game
jm build
jm run build
```

## Building Scripts

`jm build` shells out to `npx asc` from `<project>/assets/scripts/` to compile each `.ts` script to wasm. There is no Docker dependency.

### Prerequisites

- Node.js >= 20 (matches `assemblyscript`'s `engines.node`).
- npm (ships with Node).

### Starting a new project

`jm init` scaffolds a complete project in the **current directory**, including the AssemblyScript scripting setup at `assets/scripts/`. The optional `<name>` argument only sets the manifest's `name` field — it doesn't create a subdirectory:

```bash
mkdir my-game && cd my-game
jm init my-game                    # `my-game` here is the manifest name, not a subdir
cd assets/scripts && npm install   # one-time per project
cd ../.. && jm build
```

`jm init` writes `.jm.json`, an empty `scenes/main.scene.json`, the project root `.gitignore`, and the npm scaffold under `assets/scripts/` (`package.json`, `asconfig.json`, `tsconfig.json`, `.gitignore`). It does not run `npm install` — that's a one-time follow-up step you run manually so it's visible and obvious.

If `.jm.json` already exists in the current directory, `jm init` refuses to overwrite. Existing scaffold files (e.g., a hand-edited `assets/scripts/package.json`) are also preserved with a "Kept existing" message instead of being clobbered.

### First-time setup for an existing project

After cloning a project that uses the engine:

```bash
cd <project>/assets/scripts
npm install
cd ../..
jm build
```

The `npm install` step is per-project and only needs to run once (and again after `package.json` changes).

### Embedded `@jm/runtime`

The engine ships its AssemblyScript runtime (`@jm/runtime`) embedded inside the `jm` binary. On every `jm build`, the CLI extracts it into `<project>/assets/scripts/node_modules/@jm/runtime/` so `asc` and the editor's TS server can resolve `import { ... } from "@jm/runtime"`. You don't need to copy or manage these files yourself; they're regenerated from the binary each build.

### Recovering from a broken install

If `jm build` reports cryptic `asc` errors (missing modules, version mismatches, half-installed packages), wipe and reinstall:

```bash
cd <project>/assets/scripts
rm -rf node_modules
npm install
```

### CI/CD

In CI, install Node, install dependencies with `npm ci`, then run `jm build`. Example GitHub Actions step set:

```yaml
- uses: actions/setup-node@v4
  with:
    node-version-file: 'demo_game/assets/scripts/package.json'
- run: cd demo_game/assets/scripts && npm ci
- run: ./jm build
```

Use `npm ci` over `npm install` in CI for deterministic installs from `package-lock.json`.

### Migrating from the old Docker-based build

If you have a project on the old Docker-based `jm build`:

1. Pull the new `jm` binary and reinstall it (`go install ./cmd/jm` from `cli/`).
2. In your game project, install script dependencies:

```bash
cd assets/scripts
npm install
```

3. Run `jm build` from the project root.
4. The old `_temp/` directory is no longer used and can be deleted.
5. Old build containers can be cleaned up with Docker. If `docker ps -a` lists any `jm-asc-build` containers, remove them with `docker rm <id>` (or `docker container prune`).

## Project structure

The project is split into 2 parts: the cli and the engine. I wrote the CLI in Go because the language is simple and easy to read and follow, it has a rich standard (and extended standard) library including tools for manipulating images, language-first JSON marshalling and a whole lot more that the helped me write the cli faster.

The CLI entry is in `cli/cmd/jm/main.go` and each of the top-level commands are in the other `.go` files in the same folder. The CLI is built and installed using the `go` command and has nothing to do with the cmake files.

The engine was written in C++ and uses cmake to build. The main goal of the engine is to create a modular system built around the core module. The modules are split into the core module and feature modules, where features can be optional based on the build (turn off renderer for a server build, etc.).

#### The core module contains:
- `application`: the application runtime -- `Application` and `EngineModule` classes, and `Registration.hpp` for the `REGISTER_MODULE("")` macro. This also has the `GameManifest` which represents the overall settings for the game engine as a part of the `.jm.json` file
- `assets`: asset management and filesystem abstraction -- `AssetManager` and `FileSystem` classes, feature modules hook into the asset manager and set up custom asset loading.
- `async` and `tasks`: async execution management -- `JobSystem`, `TaskGraph`, `LockFreeQueue`, `ThreadPool` classes to manage async execution of job nodes with dependencies.
- `ecs`: archetype-based ECS -- `Component` struct using CRTP, `System` class, contiguous component columns grouped by signature, and JSON-driven `Prefab` instantiation. Feature modules register systems and components using the ECS and provide a deserialize method when registering the components.
- `events`: Event management, pubsub style -- `EventBus` class that feature modules use to listen to and emit events.
- `logger`: macro-wrapped `spdlog` calls -- macros in the style of `JM_LOG_XXX("...{}", x)` using the fmt syntax.
- `scripting`: WASM scripting engine backed by `wasm3` -- `ScriptManager` class and `ScriptComponent` component. Feature modules register builtin functions with the script manager that are injected into running script instances. These functions are known as 'host functions.'

#### Feature modules:
- `audio`: Multivoice and a thread-safe command queue, this manages and plays sounds. `AudioModule` registers custom loading for `.wav` and `.ogg` files using `miniaudio`. Currently the `.ogg` loading isn't working, but `.wav` is. This also exposes an `AudioEmitterComponent` and several host functions for playing, stopping and fading-out of sounds.
- `glfw_window`: GLFW window abstraction, this creates the main window using options found in the game manifest. It also hooks into events for closing the window.
- `renderer2d`: Simple sprite batch renderer and the ability to add post-effects (blur, vignette, color grading, screen shake, etc.). This exposes the `SpriteComponent` and the `Renderer2DSystem` that also uses the `TransformComponent` to render the sprites.

## EngineModule structure

### Base methods

- `initialize`: `void initialize(Application& app)` -- This initializes your module, the constructor should be default constructable and you must do all of your initialization here in this method. The `app` param can be used to access the asset manager, ecs, scripting and events.
- `shutdown`: `void shutdown(Application& app)` -- This is where you would shutdown any owned resources if needed as well as unsub from any events.
- `tickMainThread`: `void tickMainThread(Application&, float dt)` -- This method runs each frame in the main thread. This is where the audio module plays sounds, the renderer makes opengl calls, etc.
- `tickAsync`: `void tickAsync(float dt)` -- This method is wrapped in a job node each frame and added to the frame's job graph.

### Initialization

- Set up Components and Systems:
  - Register System Example: `app.getWorld().registerSystem<AudioSystem>(_audioManager);`
  - Register Component: `app.getWorld().registerComponent<AudioEmitterComponent>(...);`. This is where you parse the json object to build the component.
- Set up script host functions:
  - Register example: `app.getScriptManager().registerHostFunction("__jmPlaySound", {"env", "__jmPlaySound", "i(iifi)", &playSound});`
  - Host function example:
```cpp
m3ApiRawFunction(playSound) {
  (void)_ctx;
  (void)_mem;

  m3ApiReturnType(uint32_t);
  m3ApiGetArg(int32_t, ptr);
  m3ApiGetArg(int32_t, len);
  m3ApiGetArg(float, gain);
  m3ApiGetArg(int32_t, loopFlag);

  if (!currentAudioModule) {
    return "Audio context missing";
  }

  uint8_t* memory = m3_GetMemory(runtime, nullptr, 0);
  if (!memory) {
    return "Memory context missing";
  }

  std::string soundName(reinterpret_cast<char*>(memory + ptr), len);

  AudioHandle handle(soundName);
  bool loop = loopFlag != 0;

  SoundInstanceId instanceId = currentAudioModule->getAudioManager().play(handle, gain, loop);

  m3ApiReturn(instanceId);
}
```
- Set up custom asset loading:
  - Example with `.wav`:
```cpp
  app.getAssetManager().addAssetConverter({".wav"}, [&](const RawAsset& asset, const AssetHandle& assetHandle) {
    auto buffer = SoundBuffer::decode(asset.data);
    AudioHandle audioHandle = _audioManager.registerSound(asset.filePath.filename().string(), std::move(buffer));
    _handleMap[assetHandle] = audioHandle;
  });
```