#pragma once

#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
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

// Observes transitions (the renderer composites them). Called on the main
// thread: onBegin right after the new scene loaded — the last presented frame
// still shows the old scene — then onProgress (0..1) every tick, then onEnd.
struct TransitionListener {
  std::function<void(const TransitionConfig&)> onBegin;
  std::function<void(float progress)> onProgress;
  std::function<void()> onEnd;
};

// Owns which scene is current and which entities belong to it. One scene at a
// time: loading a scene destroys the previous scene's entities (including
// runtime-spawned ones adopted via adoptEntity).
//
// Lifecycle events (SceneUnloading, SceneLoaded, SceneTransitionStarted /
// Finished, SceneLoadFailed) go out on the EventBus. A load that throws
// (malformed JSON, bad component data) leaves no current scene, emits
// SceneLoadFailed and rethrows.
class SceneManager {
 public:
  SceneManager(World& world, AssetManager& assetManager, EventBus& eventBus);

  SceneManager(const SceneManager&) = delete;
  SceneManager& operator=(const SceneManager&) = delete;

  // Main thread. Both are ignored (with a warning) while a transition runs.
  void loadScene(const std::filesystem::path& scenePath);
  void transitionTo(const std::filesystem::path& scenePath, TransitionConfig config = {});

  // Any thread (scripts): queued and applied by the next tick(). Latest wins.
  void requestLoad(std::filesystem::path scenePath);
  void requestTransition(std::filesystem::path scenePath, TransitionConfig config = {});

  // Main thread, once per frame: applies a queued request, advances a transition.
  void tick(float dt);

  // Runtime-spawned entities join the current scene; destroyEntity removes one.
  void adoptEntity(EntityId id);
  void destroyEntity(EntityId id);

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
    bool transition = false;
    std::filesystem::path path;
    TransitionConfig config;
  };

  World& _world;
  AssetManager& _assetManager;
  EventBus& _eventBus;
  SceneLoader _loader;

  std::string _currentScenePath;
  AssetHandle _currentSceneHandle;
  std::unordered_map<EntityId, std::string> _entityToScene;
  std::optional<ActiveTransition> _transition;

  std::vector<std::function<void()>> _unloadListeners;
  std::vector<TransitionListener> _transitionListeners;

  std::mutex _requestMutex;
  std::optional<Request> _request;

  // Unloads the current scene and loads `scenePath`; returns its handle.
  AssetHandle replaceScene(const std::filesystem::path& scenePath);
  void unloadCurrentScene();
  void finishTransition();
};
