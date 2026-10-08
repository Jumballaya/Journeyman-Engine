#pragma once

#include <chrono>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <vector>

#include "../assets/AssetManager.hpp"
#include "../ecs/World.hpp"
#include "../events/EventBus.hpp"
#include "../scripting/ScriptManager.hpp"
#include "DevOptions.hpp"
#include "Seeds.hpp"
#include "EntitySpawner.hpp"
#include "EntityStores.hpp"
#include "GameClock.hpp"
#include "GameManifest.hpp"
#include "GameState.hpp"
#include "ModuleRegistry.hpp"
#include "SceneManager.hpp"

// How an Engine runs. A standalone game uses the defaults; an editor embeds one.
struct EngineOptions {
  DevOptions dev = DevOptions::fromEnvironment();
  // No window of its own: the host sizes the view (resizeView), forwards input
  // events and draws each finished frame (Renderer2D::frameTexture).
  bool embedded = false;
  bool loadEntryScene = true;
};

// The runtime: world, assets, scripting, events, scenes, modules and the frame
// loop. initialize() loads the manifest and entry scene; run() loops until Quit,
// or a host calls frame() itself.
class Engine {
 public:
  Engine(const std::filesystem::path& rootDir, const std::filesystem::path& manifestPath, EngineOptions options = {});
  ~Engine();

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  // Registers every component and binds every script host function (the
  // engine's and its modules'): the first step of initialize(), and on its own
  // all `--schema` needs (no window, GL or project). Idempotent.
  void declare();
  void initialize();
  void run();
  // One frame of `dt` seconds (clamped to kMaxDeltaTime).
  void frame(float dt);
  // Idempotent; the destructor calls it.
  void shutdown();
  // False once something asked to quit.
  bool running() const { return _running; }
  // Ends run() after the current frame.
  void quit() { _running = false; }
  uint64_t frameCount() const { return _frames; }
  // The game's state as data, for tools (JM_DUMP_DIR; EngineState.cpp): frame,
  // scene, every entity's tags, parent and components (with their script
  // fields' values), the session and save stores, and modules' parts (UI).
  nlohmann::json stateJson();
  // The stepped driver (JM_DRIVE; EngineDriver.cpp): reads commands from `in`
  // and answers each with one JSON line on `out`, advancing only when told.
  // run() uses it when driving. Returns at "quit", end of input, or a Quit.
  void drive(std::istream& in, std::ostream& out);
  // Errors the run logs, reported in the driver's next reply (Application).
  void noteError(nlohmann::json error) { _driverErrors.push_back(std::move(error)); }

  // Off = an edit preview: rendering only, no scripts, physics or animation.
  void setSimulating(bool on) { _simulating = on; }
  bool simulating() const { return _simulating; }

  bool embedded() const { return _options.embedded; }
  // Embedded only: the view's framebuffer size, and whether it has input focus.
  void resizeView(int width, int height);
  struct ViewSize {
    int width = 0, height = 0;
  };
  ViewSize viewSize() const { return _viewSize; }
  void setViewFocused(bool focused) { _viewFocused = focused; }
  bool viewFocused() const { return _viewFocused; }

  World& getWorld() { return _world; }
  AssetManager& getAssetManager() { return _assetManager; }
  ScriptManager& getScriptManager() { return _scriptManager; }
  SceneManager& getSceneManager() { return _sceneManager; }
  EventBus& getEventBus() { return _eventBus; }
  GameClock& getClock() { return _clock; }
  Seeds& getSeeds() { return _seeds; }
  EntitySpawner& getSpawner() { return _spawner; }
  const GameManifest& getManifest() const { return _manifest; }
  const DevOptions& getDevOptions() const { return _options.dev; }
  ModuleRegistry& getModules() { return _modules; }

 private:
  using Clock = std::chrono::steady_clock;
  static constexpr float kMaxDeltaTime = 0.1f;  // clamp hitches (no tunneling)

  EngineOptions _options;
  std::filesystem::path _manifestPath;
  GameManifest _manifest;
  bool _initialized = false;
  bool _declared = false;
  std::vector<nlohmann::json> _driverErrors;
  bool _running = true;
  bool _simulating = true;
  ViewSize _viewSize;
  bool _viewFocused = false;
  uint64_t _frames = 0;

  World _world;
  AssetManager _assetManager;
  ScriptManager _scriptManager;
  EventBus _eventBus{8192};
  SceneManager _sceneManager;
  GameClock _clock;
  Seeds _seeds;  // JM_SEED, or a fresh one (logged) for a played run
  EntitySpawner _spawner;
  GameState _session;                   // shared script state for this run
  std::unique_ptr<GameState> _save;     // persisted; created once the game name is known
  EntityStores _entityStores;           // entity.data
  ModuleRegistry _modules;              // last: shut down and destroyed first

  void loadManifest();
  void registerScripting();
  void bindScriptApi();  // EngineScriptApi.cpp
  // Writes stateJson() to JM_DUMP_DIR/<name> when dumping is on.
  void dumpState(const std::string& name);
  // An entity's ScriptComponent params, or null if it has no script.
  const nlohmann::json* paramsOf(EntityId id);
  void preloadAssets();
  void loadSessionFile();  // JM_SESSION
  void loadEntryScene();
};
