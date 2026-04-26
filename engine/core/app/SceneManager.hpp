#pragma once

#include <filesystem>
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
  // scene is loaded — skips the unload path.
  void loadScene(const std::filesystem::path& scenePath);

  // Begin a transition. Stub in D.1; D.4 fills in the state machine.
  void transitionTo(const std::filesystem::path& scenePath,
                    TransitionConfig config = {});

  // Advance any in-flight transition. Called from Engine::run on the main
  // thread, after tickMainThreadModules. Stub in D.1.
  void tick(float dt);

  const std::string& getCurrentScenePath() const { return _currentScenePath; }
  AssetHandle getCurrentSceneHandle() const { return _currentSceneHandle; }
  bool isTransitioning() const { return _phase != Phase::Idle; }
  const TransitionState& getTransitionState() const { return _transitionState; }

 private:
  enum class Phase { Idle, Transitioning };

  struct EntityRegistration {
    std::string scenePath;
  };

  World& _world;
  AssetManager& _assetManager;
  EventBus& _eventBus;
  SceneLoader _loader;

  std::string _currentScenePath;
  AssetHandle _currentSceneHandle;
  std::unordered_map<EntityId, EntityRegistration> _entityToScene;

  Phase _phase = Phase::Idle;
  TransitionState _transitionState;

  void unloadCurrentScene();
};
