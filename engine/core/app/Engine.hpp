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

// The runtime: world, assets, scripting, events, scenes, modules and the frame
// loop. initialize() loads the manifest and entry scene; run() loops until Quit.
class Engine {
 public:
  Engine(const std::filesystem::path& rootDir, const std::filesystem::path& manifestPath);
  ~Engine();

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  void initialize();
  void run();

  World& getWorld() { return _world; }
  AssetManager& getAssetManager() { return _assetManager; }
  ScriptManager& getScriptManager() { return _scriptManager; }
  SceneManager& getSceneManager() { return _sceneManager; }
  EventBus& getEventBus() { return _eventBus; }
  GameClock& getClock() { return _clock; }
  EntitySpawner& getSpawner() { return _spawner; }
  const GameManifest& getManifest() const { return _manifest; }
  const DevOptions& getDevOptions() const { return _dev; }

 private:
  using Clock = std::chrono::steady_clock;
  static constexpr float kMaxDeltaTime = 0.1f;  // clamp hitches (no tunneling)

  DevOptions _dev = DevOptions::fromEnvironment();
  std::filesystem::path _manifestPath;
  GameManifest _manifest;
  bool _running = false;

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

  void loadManifest();
  void registerScripting();
  void bindScriptApi();  // EngineScriptApi.cpp
  // An entity's ScriptComponent params, or null if it has no script.
  const nlohmann::json* paramsOf(EntityId id);
  void preloadAssets();
  void loadEntryScene();
  void shutdown();
};
