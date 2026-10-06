# Journeyman Game Engine

A small, modular 2D game engine written in C++ with games scripted in
AssemblyScript (compiled to WebAssembly) and a Go CLI (`jm`) that works like a
headless editor: scaffold, build, run, pack, and export standalone games.

This engine is for educational purposes and not meant to be a _real_ game
engine. I hope you like it, and I hope you can learn something from it!

The repo ships a complete demo game, **Strike Wing 1942** (`demos/strike_wing/`): a
1942-style vertical shooter with a title menu, two stages and a boss fight,
pause menu, results screens between stages, game over and victory screens,
saved high score and options. See [demos/strike_wing/README.md](demos/strike_wing/README.md).

## The editor

`journeyman_editor` is a desktop editor for Journeyman projects: scenes in a
live viewport with gizmos, a schema-driven inspector, tile-map painting,
prefabs, an asset browser, play-in-editor, a command palette, and one-click
export of a standalone game. See [docs/editor.md](docs/editor.md).

![The Journeyman editor painting a tile map](docs/images/editor.png)

```bash
./scripts/build-release.sh                       # engine + editor
(cd cli && go build -o ../build/bin/jm ./cmd/jm) # the CLI the editor drives
./build/release/editor/journeyman_editor
./scripts/package-editor.sh                      # dist/Journeyman Editor.app (jm + engine inside)
./scripts/install_mac.sh                         # build and install in /Applications (macOS)
```

## Play the demo

```bash
./scripts/play-demo.sh            # build everything and run in a window
./scripts/play-demo.sh --export   # build a standalone app in demos/strike_wing/dist/
```

Controls: arrows/WASD move, Space/Z fire, X bomb, Esc/P pause, F11 fullscreen.
Gamepads work too (stick/D-pad, A fire, B bomb, Start pause).

## Requirements

- CMake ≥ 3.20 and Ninja (dependencies are fetched by CMake)
- A C++23 compiler (Apple Clang 15+, GCC 13+, MSVC 17.8+)
- Go 1.24+ for the `jm` CLI
- Node.js ≥ 20 and npm (scripts compile with `npx asc`)
- macOS 11+ (OpenGL 4.1) or Linux/Windows with OpenGL 4.6

With CMake ≥ 4.0 the presets already set `CMAKE_POLICY_VERSION_MINIMUM=3.5`
(some wasm3 packages require it).

## Build

```bash
./scripts/build-release.sh    # engine → build/release/engine/journeyman_engine
./scripts/build-debug.sh      # engine with trace logging → build/debug/...
./scripts/build-tests.sh      # C++ unit tests (ctest) + CLI tests (go test)

cd cli && go install ./cmd/jm # the CLI (or: go build -o jm ./cmd/jm)
```

## Making a game

```bash
mkdir my-game && cd my-game
jm init "My Game"                  # .jm.json, scenes/main.scene.json, scripts npm project
(cd assets/scripts && npm install)  # one-time: AssemblyScript

jm generate script player          # assets/scripts/player.ts (auto-registered)
jm generate prefab bullet          # assets/prefabs/bullet.prefab.json
jm generate ui hud                 # assets/ui/hud.ui.html
jm generate shader crt             # assets/shaders/crt.frag
jm generate bindings input         # assets/input.bindings.json
jm generate scene level2
jm generate list                   # everything generate can make

jm build                           # compile scripts, bake atlases → build/
jm test                            # run tests/*.spec.ts (game logic, no build needed)
jm run                             # run build/ in the engine
jm pack                            # one archive: build/<name>.jm
jm run build/my-game.jm            # run the archive
jm export                          # standalone game: dist/<Name>.app (macOS) or dist/<Name>/
jm migrate                         # convert an older project (.script.json) to .ts scripts
```

Point `"engine"` in `.jm.json` at your engine binary (relative to the project
root, the build directory, or an absolute path), or put `journeyman_engine`
on your `PATH`.

The project tree holds only sources you author: `.jm.json`, scenes, prefabs,
`.ts` scripts, images, sounds, fonts, `.ui.html`/`.css` screens, `.frag`
shaders, atlas configs and input bindings. `jm build` produces everything
else in `build/` (CLI-owned and wiped on every build).

### Exported games

`jm export` builds the game, packs it into one archive and appends that to a
copy of the engine: the result is a single executable with everything inside
(`dist/<Name>.app` on macOS, `--bare` for the binary alone; `dist/<Name>`
on Linux; `dist/<Name>.exe` on Windows). The engine finds the archive inside
itself through a footer, which survives code signing; macOS exports are
signed ad hoc and pass `codesign --strict`. `--target os-arch` exports for
another platform using that platform's engine build (the `players` CI
workflow builds them). A double-clicked game logs to and saves in the
per-user data directory (macOS: `~/Library/Application Support/<Name>/`).
Set `config.export.icon` (a PNG) for a macOS app icon.

## Documentation

- [Scripting API](docs/scripting.md) — everything `@jm/runtime` exposes to
  game scripts: entities, spawning, components, input actions, audio, UI,
  scenes & transitions, post-effects, camera, time & pause, game state & saves.
- [Content & data formats](docs/content.md) — `.jm.json` config, scenes,
  prefabs, all built-in components, atlases & animation, the HTML/CSS UI
  subset, shaders, input bindings, audio.
- [Editor](docs/editor.md) — the workspace, scene editing, tile painting,
  play-in-editor, export, shortcuts and automation.
- [Testing & automation](docs/testing.md) — unit tests, headless runs,
  input replay, frame capture.

## Project structure

The project is split into 2 parts: the cli and the engine. I wrote the CLI in Go because the language is simple and easy to read and follow, it has a rich standard (and extended standard) library including tools for manipulating images, language-first JSON marshalling and a whole lot more that the helped me write the cli faster.

The CLI entry is in `cli/cmd/jm/main.go` and each of the top-level commands are in the other `.go` files in the same folder. The CLI is built and installed using the `go` command and has nothing to do with the cmake files. The AssemblyScript runtime (`@jm/runtime`) lives in `cli/internal/stdlib/runtime/`; it is embedded in the `jm` binary and extracted into each project's `node_modules` on build.

The engine was written in C++ and uses cmake to build. The main goal of the engine is to create a modular system built around the core module. The modules are split into the core module and feature modules, where features can be optional based on the build (turn off renderer for a server build, etc.).

#### The core module contains:
- `app`: the runtime — `Application` (process shell, argv, logging, standalone archive discovery), `Engine` (frame loop, manifest, `GameClock`, `GameState` stores, `EntitySpawner`), `SceneManager` (scene lifecycle and shader transitions), `EngineModule`, `ModuleRegistry` and the `REGISTER_MODULE` macro.
- `assets`: asset management and filesystem abstraction — `AssetManager`, `FileSystem`, and the `.jm` `Archive` reader. Feature modules register converters per extension (folder mode) and per type (archive mode).
- `async` and `tasks`: `JobSystem`, `TaskGraph`, `LockFreeQueue`, `ThreadPool` (work-stealing; idle workers sleep).
- `ecs`: archetype-based ECS — CRTP `Component`s, `System`s scheduled by declared data access (conflicting systems never run concurrently; see `SystemTraits.hpp`), JSON prefabs with deep-merged overrides, tags, deferred destruction.
- `events`: pub/sub `EventBus` with a lock-free queue drained on the main thread.
- `logger`: macro-wrapped `spdlog` calls — `JM_LOG_XXX("...{}", x)`.
- `scripting`: WASM scripting backed by `wasm3` — `ScriptManager`, `ScriptComponent` and the host-function plumbing. Scripting is core, not a feature module: every game needs it.

#### Feature modules:
- `audio`: miniaudio device + mixer. All voice state lives on the audio thread and is driven by a lock-free command queue; master/music/sfx buses, sample-accurate fades, voice stealing, soft limiter. `.wav`/`.ogg`/`.mp3`/`.flac`.
- `glfw_window`: the window, fullscreen toggling, headless mode for automation.
- `inputs`: keyboard state (modifiers included), gamepads (GLFW gamepad mappings), named actions from `.bindings.json`, auto-repeat, input replay.
- `physics2d`: transforms, velocities and accelerations, AABB colliders with layer masks (calls `onCollide` on scripts), lifetimes, scroll-wrapping.
- `tilemap`: ASCII tile maps over JSON tilesets (edge-aware auto-tiling, animated tiles, tags), drawn per view without per-tile entities; scripts query them and move boxes through them.
- `renderer2d`: z-sorted instanced sprite batching at a fixed logical resolution (letterboxed, DPI-independent), texture atlases and sprite animation, a screen-space UI pass, post-effect chain with builtin and custom `.frag` shaders, shader-composited scene transitions, camera shake, frame capture.
- `ui`: HTML/CSS screens (`.ui.html`) — parser, cascade, flexbox-subset layout, TrueType text rendered through glyph atlases — and world-space text (`TextComponent`).

## EngineModule structure

### Base methods

- `initialize`: `void initialize(Engine& app)` -- This initializes your module, the constructor should be default constructable and you must do all of your initialization here in this method. The `app` param can be used to access the asset manager, ecs, scripting and events.
- `shutdown`: `void shutdown(Engine& app)` -- This is where you would shutdown any owned resources if needed as well as unsub from any events.
- `tickMainThread`: `void tickMainThread(Engine&, float dt)` -- This method runs each frame in the main thread. This is where the audio module plays sounds, the renderer makes opengl calls, etc.
- `tickAsync`: `void tickAsync(float dt)` -- This method is wrapped in a job node each frame and added to the frame's job graph.

Modules declare dependencies with `ModuleTraits<T>` (`Provides`/`DependsOn`
tag lists from `ModuleTags.hpp`); each `Engine` builds its own
`ModuleRegistry` from the `REGISTER_MODULE` catalog and initializes them in
dependency order. A module can reach another via
`app.getModules().find<OtherModule>()` once it depends on its tag.

### Initialization

- Register systems: `app.getWorld().registerSystem<AudioSystem>(_audio);`. Give each a `SystemTraits` specialization (reads/writes/stage) so the scheduler knows what it may run alongside; undeclared systems run exclusively.
- Register components with a `ComponentSpec` (every member optional): how to read scene/prefab JSON, which fields scripts may touch, what to release when an entity dies, and a schema describing the JSON for the editor's inspector:
```cpp
app.getWorld().registerComponent<HealthComponent>({
    .fromJson = [](HealthComponent& c, const nlohmann::json& json, EntityId) {
      c.hp = json.value("hp", 3.0f);
    },
    .scriptFields = {scriptField<HealthComponent>("hp", [](HealthComponent& c) -> float& { return c.hp; })},
    .onDestroy = [this](HealthComponent& c) { /* free external resources */ },
    .schema = {"Health", "Gameplay", "Hit points; 0 destroys the entity",
               {FieldSchema::number("hp", 3, "Starting hit points", 0, 100, 1)}},
});
```
  Scripts then use `new Field("HealthComponent", "hp")` (script fields are 4-byte `float`s or `uint32_t`s).
- Expose functions to scripts by binding ordinary lambdas; the wasm signature and argument decoding come from the C++ types (`std::string`, `EntityId`, numbers, `bool`, `ScriptCall&` for the calling script; see `scripting/HostBinding.hpp`):
```cpp
app.getScriptManager().bind("__jmSoundPlay", [this](std::string name, float gain, bool loop, int32_t bus) {
  return _audio.play(AudioHandle(name), gain, loop, bus == 1 ? AudioBus::Music : AudioBus::Sfx);
});
```
  Declare the import in the runtime (`cli/internal/stdlib/runtime/env.ts`) and wrap it in a friendly API there. Bad script pointers and C++ exceptions trap only the calling script.
- Host functions run on a worker thread while the script system has the world to itself: they may read and write components, but structural changes (spawn/destroy) go through `EntitySpawner` / `World::destroyDeferred`, and anything touching GL or other main-thread state must be queued for `tickMainThread`.
- Set up custom asset loading (register both the extension and the archive type):
```cpp
  auto decoder = [this](const RawAsset& asset, const AssetHandle& handle) {
    _audio.insert(handle, decode(asset.data));
  };
  app.getAssetManager().addAssetConverter({".wav"}, decoder);   // folder mode
  app.getAssetManager().addAssetTypeConverter("audio", decoder); // archive mode
```
