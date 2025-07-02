#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>

#include "../assets/AssetManager.hpp"
#include "../ecs/World.hpp"
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
  void shutdown();

  World& getWorld();
  JobSystem& getJobSystem();
  AssetManager& getAssetManager();
  ScriptManager& getScriptManager();
  const GameManifest& getManifest() const;

 private:
  using Clock = std::chrono::high_resolution_clock;
  Clock::time_point _previousFrameTime;
  float _maxDeltaTime = 0.33f;  // clamping to 30 FPS at max
  bool running = false;

  std::filesystem::path _manifestPath;
  std::filesystem::path _rootDir;
  GameManifest _manifest;

  World _ecsWorld;
  JobSystem _jobSystem;
  AssetManager _assetManager;
  SceneLoader _sceneLoader;
  ScriptManager _scriptManager;

  GameManifest parseGameManifest(const nlohmann::json& json);
};