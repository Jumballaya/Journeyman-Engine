#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>

#include "../assets/AssetManager.hpp"
#include "../ecs/World.hpp"
#include "../events/EventBus.hpp"
#include "../scripting/ScriptManager.hpp"
#include "../tasks/JobSystem.hpp"
#include "GameManifest.hpp"
#include "ModuleRegistry.hpp"
#include "SceneLoader.hpp"

class Application {
 public:
  Application(const std::filesystem::path& rootDir, const std::filesystem::path& manifestPath);
  ~Application();

  void initialize();
  void run();
  void abort();
  void shutdown();

  World& getWorld();
  JobSystem& getJobSystem();
  AssetManager& getAssetManager();
  ScriptManager& getScriptManager();
  const GameManifest& getManifest() const;

  EventBus& getEventBus() { return _eventBus; }
  const EventBus& getEventBus() const { return _eventBus; }

 private:
  using Clock = std::chrono::high_resolution_clock;
  Clock::time_point _previousFrameTime;
  float _maxDeltaTime = 0.33f;  // clamping to 30 FPS at max
  bool _running = false;

  std::filesystem::path _manifestPath;
  std::filesystem::path _rootDir;
  GameManifest _manifest;

  World _ecsWorld;
  JobSystem _jobSystem;
  AssetManager _assetManager;
  SceneLoader _sceneLoader;
  ScriptManager _scriptManager;
  EventBus _eventBus{8192};

  void initializeCoreECS();
  void loadAndParseManifest();
  void registerScriptModule();
  void initializeGameFiles();
  void loadScenes();
};