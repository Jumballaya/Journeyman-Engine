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

//
//  @TODO: Have a global ECS and a per-scene ECS, that means getWorld() needs to figure out
//         which world to get (global or scene)
//         The same goes for the serialization/deserialization and component/system
//         registration by modules
//         ECS needs to create SystemRegistry and move SystemRegistry and ComponentRegistry
//         into an ECSRegistry that can be injected into the World class when they are created
//
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

  void loadAndParseManifest();
  void registerScriptModule();
  void initializeGameFiles();
  void loadScenes();
};