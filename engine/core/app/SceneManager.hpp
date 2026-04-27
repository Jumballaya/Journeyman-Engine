#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "../assets/AssetHandle.hpp"
#include "../assets/AssetManager.hpp"
#include "../ecs/World.hpp"
#include "../ecs/entity/EntityId.hpp"
#include "../events/EventBus.hpp"
#include "SceneLoader.hpp"

// Configuration for a shader-composited transition. Duration is in seconds.
// Future fields (custom shader, easing curve) live here when D adds them.
struct TransitionConfig {
  float duration = 0.5f;
};

// Read-only snapshot of the active transition, exposed via
// getTransitionState() so the renderer (or any other observer) can poll
// each frame. POD only — callers do not reach into SceneManager internals.
struct TransitionState {
  bool active = false;
  float progress = 0.0f;
  AssetHandle fromScene;
  AssetHandle toScene;
  float duration = 0.0f;
};

// SceneManager owns scene-level entity lifecycle: which scene is current,
// which entities belong to it, and when to destroy them on a swap. It is
// renderer-blind; the renderer module polls getTransitionState() to handle
// the visual side.
//
// Single active scene at a time (no additive loads). loadScene is a
// "replace" — destroy current → load new. transitionTo schedules a
// shader-composited swap (D.4 fills in the visuals; D.1 stubs it).
class SceneManager {
 public:
  SceneManager(World& world, AssetManager& assetManager, EventBus& eventBus);
  ~SceneManager() = default;

  SceneManager(const SceneManager&) = delete;
  SceneManager& operator=(const SceneManager&) = delete;

  // Immediate scene swap: destroy the current scene's entities, load the
  // new scene, fire lifecycle events. Blocking. Safe to call before any
  // scene is loaded — skips the unload path. If the new scene fails to load
  // (malformed JSON, bad component, missing prefab) the previous scene has
  // already been unloaded; SceneManager fires SceneLoadFailed and re-throws
  // so the caller sees the failure. After a failed load, no current scene
  // is set and the world is empty.
  void loadScene(const std::filesystem::path& scenePath);

  // Begin a shader-composited transition. Logical only — destroys outgoing
  // entities and loads incoming entities synchronously, then leaves the
  // transition state armed so tick() can animate progress for the renderer
  // to poll. REJECTED if called during an active transition (logs warning,
  // returns); same policy applies to loadScene. See SceneManager.cpp for
  // the rationale (latest-wins replacement was considered and rejected).
  // On a load failure: fires SceneLoadFailed, re-throws, and never fires
  // SceneTransitionStarted or SceneTransitionFinished — the Started/Finished
  // pair is symmetric on the happy path and absent together on failure.
  void transitionTo(const std::filesystem::path& scenePath,
                    TransitionConfig config = {});

  // Advance any in-flight transition. Called from Engine::run on the main
  // thread, after tickMainThreadModules. Drains any pending request queued
  // from a worker thread before advancing the active transition.
  void tick(float dt);

  // Thread-safe entry points for non-main-thread callers (script host
  // functions). Enqueue a request; tick() drains and applies on the main
  // thread. From main thread code, prefer loadScene/transitionTo directly.
  // Latest-wins: a pending request overwrites any earlier one.
  void requestLoad(std::filesystem::path scenePath);
  void requestTransition(std::filesystem::path scenePath,
                         TransitionConfig config = {});

  const std::string& getCurrentScenePath() const { return _currentScenePath; }
  AssetHandle getCurrentSceneHandle() const { return _currentSceneHandle; }
  bool isTransitioning() const { return _phase != Phase::Idle; }
  const TransitionState& getTransitionState() const { return _transitionState; }

 private:
  enum class Phase { Idle, Transitioning };

  struct EntityRegistration {
    std::string scenePath;
  };

  struct ActiveTransition {
    std::string targetPath;
    AssetHandle fromHandle;
    AssetHandle toHandle;
    TransitionConfig config;
    float elapsed = 0.0f;
  };

  struct PendingRequest {
    enum class Kind { Load, Transition };
    Kind kind = Kind::Load;
    std::filesystem::path path;
    TransitionConfig config{};
  };

  World& _world;
  AssetManager& _assetManager;
  EventBus& _eventBus;
  SceneLoader _loader;

  std::string _currentScenePath;
  AssetHandle _currentSceneHandle;
  std::unordered_map<EntityId, EntityRegistration> _entityToScene;

  Phase _phase = Phase::Idle;
  std::optional<ActiveTransition> _activeTransition;
  TransitionState _transitionState;

  std::mutex _requestMutex;
  std::optional<PendingRequest> _pendingRequest;

  void unloadCurrentScene();
  void finishTransition();
  void refreshTransitionState();
};
