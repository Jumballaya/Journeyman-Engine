#pragma once

#include <chrono>
#include <filesystem>
#include <memory>

#include "../assets/AssetManager.hpp"
#include "../ecs/World.hpp"
#include "../events/EventBus.hpp"
#include "../scripting/ScriptManager.hpp"
#include "../tasks/JobSystem.hpp"
#include "DevOptions.hpp"
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

  void initialize();
  void run();
  // One frame of `dt` seconds (clamped to kMaxDeltaTime).
  void frame(float dt);
  // Idempotent; the destructor calls it.
  void shutdown();
  // False once something asked to quit.
  bool running() const { return _running; }

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
  bool _running = true;
  bool _simulating = true;
  ViewSize _viewSize;
  bool _viewFocused = false;
  uint64_t _frames = 0;

  World _world;
  JobSystem _jobSystem;
  AssetManager _assetManager;
  ScriptManager _scriptManager;
  EventBus _eventBus{8192};
  SceneManager _sceneManager;
  GameClock _clock;
  EntitySpawner _spawner;
  GameState _session;                   // shared script state for this run
  std::unique_ptr<GameState> _save;     // persisted; created once the game name is known
  EntityStores _entityStores;           // entity.data
  ModuleRegistry _modules;              // last: shut down and destroyed first

  void loadManifest();
  void registerScripting();
  void bindScriptApi();  // EngineScriptApi.cpp
  // An entity's ScriptComponent params, or null if it has no script.
  const nlohmann::json* paramsOf(EntityId id);
  void preloadAssets();
  void loadEntryScene();
};
