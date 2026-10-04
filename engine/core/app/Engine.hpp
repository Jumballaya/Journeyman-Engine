#pragma once

#include <chrono>
#include <filesystem>

#include "../assets/AssetManager.hpp"
#include "../ecs/World.hpp"
#include "../events/EventBus.hpp"
#include "../scripting/ScriptManager.hpp"
#include "../tasks/JobSystem.hpp"
#include "EntitySpawner.hpp"
#include "GameClock.hpp"
#include "GameManifest.hpp"
#include "GameState.hpp"
#include "ModuleRegistry.hpp"
#include "SceneManager.hpp"

// Engine is the runtime: world, jobs, assets, scripting, events, modules, and
// the frame loop. It also loads the game manifest and entry scene during
// initialize() — these are engine-level concerns because feature modules read
// the manifest during their own init pass, and scenes are the engine's unit
// of entity content.
class Engine {
 public:
  Engine(const std::filesystem::path& rootDir, const std::filesystem::path& manifestPath);
  ~Engine();

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  void initialize();  // parse manifest, register script core, init modules, preload assets, load entry scene
  void run();         // frame loop; returns when events::Quit is emitted
  void abort();
  void shutdown();

  World& getWorld();
  JobSystem& getJobSystem();
  AssetManager& getAssetManager();
  ScriptManager& getScriptManager();
  SceneManager& getSceneManager() { return _sceneManager; }
  const SceneManager& getSceneManager() const { return _sceneManager; }
  const GameManifest& getManifest() const { return _manifest; }

  EventBus& getEventBus() { return _eventBus; }
  const EventBus& getEventBus() const { return _eventBus; }

  GameClock& getClock() { return _clock; }
  EntitySpawner& getSpawner() { return _spawner; }
  // Session state lives for the process; save state is persisted to the
  // per-user data directory and flushed at the end of every frame.
  GameState& getSessionState() { return _sessionState; }
  GameState& getSaveState() { return *_saveState; }

 private:
  using Clock = std::chrono::high_resolution_clock;
  Clock::time_point _previousFrameTime;
  float _maxDeltaTime = 0.1f;  // longer hitches are clamped (no tunneling)
  bool _running = false;

  // Test/automation hooks read from the environment at initialize():
  //   JM_FIXED_DT=<seconds>       deterministic frame step
  //   JM_EXIT_AFTER_FRAMES=<n>    quit cleanly after n frames
  float _fixedDt = 0.0f;
  uint64_t _exitAfterFrames = 0;
  uint64_t _frameCount = 0;

  std::filesystem::path _rootDir;
  std::filesystem::path _manifestPath;
  GameManifest _manifest;

  World _ecsWorld;
  JobSystem _jobSystem;
  AssetManager _assetManager;
  ScriptManager _scriptManager;
  EventBus _eventBus{8192};
  SceneManager _sceneManager;
  GameClock _clock;
  EntitySpawner _spawner;
  GameState _sessionState;
  std::unique_ptr<GameState> _saveState;

  void loadAndParseManifest();
  void registerScriptModule();
  void initializeGameFiles();
  void loadScenes();
};
