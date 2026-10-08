#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../assets/AssetHandle.hpp"
#include "../assets/AssetManager.hpp"
#include "../ecs/World.hpp"
#include "../ecs/entity/EntityId.hpp"
#include "../events/EventBus.hpp"
#include "SceneLoader.hpp"

// A shader-composited scene change. `shader` is a .frag asset path; empty
// means the renderer's crossfade.
struct TransitionConfig {
  float duration = 0.5f;
  std::string shader;
};

// Observes transitions (the renderer composites them), on the main thread: onBegin
// after the new scene loads, onProgress (0..1) each tick, then onEnd.
struct TransitionListener {
  std::function<void(const TransitionConfig&)> onBegin;
  std::function<void(float progress)> onProgress;
  std::function<void()> onEnd;
};

// The current scene and the entities it owns (spawned ones too); loading one
// destroys the last. A failed load leaves no scene, emits SceneLoadFailed, rethrows.
class SceneManager {
 public:
  SceneManager(World& world, AssetManager& assetManager, EventBus& eventBus);

  SceneManager(const SceneManager&) = delete;
  SceneManager& operator=(const SceneManager&) = delete;

  // Main thread. Both are ignored (with a warning) while a transition runs.
  void loadScene(const std::filesystem::path& scenePath);
  void transitionTo(const std::filesystem::path& scenePath, TransitionConfig config = {});

  // For scripts: queued and applied by the next tick(), or once a
  // running transition ends. Latest wins; a failed load is logged, not thrown.
  void requestLoad(std::filesystem::path scenePath);
  void requestTransition(std::filesystem::path scenePath, TransitionConfig config = {});

  // Main thread, once per frame: applies a queued request, advances a transition.
  void tick(float dt);

  // Runtime-spawned entities join the current scene; destroyEntity removes one.
  void adoptEntity(EntityId id);
  void destroyEntity(EntityId id);
  // A scene entry ({"name", "components"} or {"prefab", "overrides"}) spawned
  // into the current scene. Throws if a component fails to build.
  EntityId spawn(const nlohmann::json& entityJson);

  // Groups: the current scene's entries marked "group" (rooms, waves...).
  // Spawning one builds its entries whose conditions hold (no-op if already
  // spawned); despawning destroys those still alive, so the next spawn starts
  // the group afresh. Main thread; scripts use requestGroup.
  void spawnGroup(const std::string& group);
  void despawnGroup(const std::string& group);
  bool groupSpawned(const std::string& group) const;
  // For scripts: applied by the next tick(), in order.
  void requestGroup(std::string group, bool spawn);
  // How "if" / "unless" keys are judged (the game state).
  void setCondition(SceneLoader::Condition condition) { _loader.setCondition(std::move(condition)); }
  // Destroys the current scene's entities, leaving no scene.
  void unload();

  // Synchronous hooks (main thread). Unload listeners run before the next
  // scene's entities exist, unlike the end-of-frame SceneUnloading event.
  void addUnloadListener(std::function<void()> listener) { _unloadListeners.push_back(std::move(listener)); }
  void addTransitionListener(TransitionListener listener) { _transitionListeners.push_back(std::move(listener)); }

  const std::string& getCurrentScenePath() const { return _currentScenePath; }
  AssetHandle getCurrentSceneHandle() const { return _currentSceneHandle; }
  bool isTransitioning() const { return _transition.has_value(); }

 private:
  struct ActiveTransition {
    AssetHandle from, to;
    TransitionConfig config;
    float elapsed = 0.0f;
  };
  struct Request {
    std::filesystem::path path;
    std::optional<TransitionConfig> transition;
  };

  World& _world;
  AssetManager& _assetManager;
  EventBus& _eventBus;
  SceneLoader _loader;

  std::string _currentScenePath;
  AssetHandle _currentSceneHandle;
  std::unordered_set<EntityId> _sceneEntities;
  std::optional<ActiveTransition> _transition;

  std::vector<std::function<void()>> _unloadListeners;
  std::vector<TransitionListener> _transitionListeners;

  std::optional<Request> _request;
  std::vector<std::pair<std::string, bool>> _groupRequests;  // group, spawn?

  std::unordered_map<std::string, std::vector<EntityId>> _spawnedGroups;

  // loadScene, or transitionTo when `transition` is set.
  void changeScene(const std::filesystem::path& scenePath, std::optional<TransitionConfig> transition);
  void finishTransition();
};
