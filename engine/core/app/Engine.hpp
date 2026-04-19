#pragma once

#include <chrono>
#include <filesystem>

#include "../assets/AssetManager.hpp"
#include "../ecs/World.hpp"
#include "../events/EventBus.hpp"
#include "../scripting/ScriptManager.hpp"
#include "../tasks/JobSystem.hpp"
#include "GameManifest.hpp"
#include "ModuleRegistry.hpp"
#include "SceneLoader.hpp"

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
  const GameManifest& getManifest() const { return _manifest; }

  EventBus& getEventBus() { return _eventBus; }
  const EventBus& getEventBus() const { return _eventBus; }

 private:
  using Clock = std::chrono::high_resolution_clock;
  Clock::time_point _previousFrameTime;
  float _maxDeltaTime = 0.33f;  // clamping to 30 FPS at max
  bool _running = false;

  std::filesystem::path _rootDir;
  std::filesystem::path _manifestPath;
  GameManifest _manifest;

  World _ecsWorld;
  JobSystem _jobSystem;
  AssetManager _assetManager;
  SceneLoader _sceneLoader;
  ScriptManager _scriptManager;
  EventBus _eventBus{8192};

  void loadAndParseManifest();
  void registerScriptModule();
  void initializeGameFiles();
  void loadScenes();
};
