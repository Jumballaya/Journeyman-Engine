#include "SceneManager.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include "../logger/logging.hpp"
#include "ApplicationEvents.hpp"

SceneManager::SceneManager(World& world, AssetManager& assetManager,
                           EventBus& eventBus)
    : _world(world), _assetManager(assetManager), _eventBus(eventBus),
      _loader(world, assetManager) {}

void SceneManager::loadScene(const std::filesystem::path& scenePath) {
  // REJECT during active transition. A mid-transition load would leave the
  // renderer's transition snapshot referencing destroyed entities — or worse,
  // tear down the new scene before the crossfade has finished resolving.
  // Demo scripts guard with !Scene.isTransitioning(); they're expected to.
  if (_phase == Phase::Transitioning) {
    JM_LOG_WARN(
        "[SceneManager] loadScene rejected: a transition is already in "
        "flight (target='{}', request='{}')",
        _activeTransition.has_value() ? _activeTransition->targetPath : "",
        scenePath.string());
    return;
  }

  // Resolve the handle first. AssetManager throws on missing files; do this
  // before touching the current scene so a bad path leaves state untouched.
  AssetHandle newHandle = _assetManager.loadAsset(scenePath);

  if (_currentSceneHandle.isValid()) {
    _eventBus.emit(EVT_SceneUnloading,
                   events::SceneUnloading{_currentSceneHandle});
    unloadCurrentScene();
  }

  std::vector<EntityId> created = _loader.loadScene(newHandle);
  _entityToScene.reserve(_entityToScene.size() + created.size());
  for (EntityId id : created) {
    _entityToScene[id] = EntityRegistration{scenePath.string()};
  }

  _currentScenePath = scenePath.string();
  _currentSceneHandle = newHandle;

  _eventBus.emit(EVT_SceneLoaded, events::SceneLoaded{newHandle});
}

void SceneManager::transitionTo(const std::filesystem::path& scenePath,
                                TransitionConfig config) {
  // REJECT during active transition. Latest-wins replacement was considered
  // and rejected: it would require a generation counter on the renderer side
  // to detect mid-flight target changes (the polling renderer only tracks
  // active-true / active-false edges, not "transitioning to a different
  // scene now"). Demo scripts guard with !Scene.isTransitioning().
  if (_phase == Phase::Transitioning) {
    JM_LOG_WARN(
        "[SceneManager] transitionTo rejected: already transitioning to '{}' "
        "(request='{}')",
        _activeTransition.has_value() ? _activeTransition->targetPath : "",
        scenePath.string());
    return;
  }

  // Resolve target handle up-front so a bad path leaves state untouched.
  AssetHandle toHandle = _assetManager.loadAsset(scenePath);
  AssetHandle fromHandle = _currentSceneHandle;

  _eventBus.emit(EVT_SceneTransitionStarted,
                 events::SceneTransitionStarted{fromHandle, toHandle,
                                                config.duration});

  if (_currentSceneHandle.isValid()) {
    _eventBus.emit(EVT_SceneUnloading,
                   events::SceneUnloading{_currentSceneHandle});
    unloadCurrentScene();
  }

  std::vector<EntityId> created = _loader.loadScene(toHandle);
  _entityToScene.reserve(_entityToScene.size() + created.size());
  for (EntityId id : created) {
    _entityToScene[id] = EntityRegistration{scenePath.string()};
  }

  _currentScenePath = scenePath.string();
  _currentSceneHandle = toHandle;

  _eventBus.emit(EVT_SceneLoaded, events::SceneLoaded{toHandle});

  _phase = Phase::Transitioning;
  _activeTransition = ActiveTransition{
      scenePath.string(), fromHandle, toHandle, config, 0.0f};
  refreshTransitionState();

  // Duration <= 0: finish on this same call. The renderer never sees a rising
  // edge for this transition (state.active flips from false→true→false within
  // a single Engine::run iteration before the renderer's next tickMainThread).
  if (config.duration <= 0.0f) {
    finishTransition();
  }
}

void SceneManager::tick(float dt) {
  // Drain any pending cross-thread request first. Apply on this same tick so
  // the resulting state is visible to subsequent observers in the frame.
  std::optional<PendingRequest> drained;
  {
    std::lock_guard<std::mutex> lock(_requestMutex);
    if (_pendingRequest.has_value()) {
      drained = std::move(_pendingRequest);
      _pendingRequest.reset();
    }
  }
  if (drained.has_value()) {
    if (drained->kind == PendingRequest::Kind::Load) {
      loadScene(drained->path);
    } else {
      transitionTo(drained->path, drained->config);
    }
  }

  if (_phase != Phase::Transitioning || !_activeTransition.has_value()) {
    return;
  }

  ActiveTransition& t = *_activeTransition;
  t.elapsed += dt;

  float duration = t.config.duration;
  float progress = duration > 0.0f
                       ? std::clamp(t.elapsed / duration, 0.0f, 1.0f)
                       : 1.0f;
  _transitionState.progress = progress;

  if (progress >= 1.0f) {
    finishTransition();
  }
}

void SceneManager::requestLoad(std::filesystem::path scenePath) {
  std::lock_guard<std::mutex> lock(_requestMutex);
  PendingRequest req;
  req.kind = PendingRequest::Kind::Load;
  req.path = std::move(scenePath);
  _pendingRequest = std::move(req);
}

void SceneManager::requestTransition(std::filesystem::path scenePath,
                                     TransitionConfig config) {
  std::lock_guard<std::mutex> lock(_requestMutex);
  PendingRequest req;
  req.kind = PendingRequest::Kind::Transition;
  req.path = std::move(scenePath);
  req.config = config;
  _pendingRequest = std::move(req);
}

void SceneManager::unloadCurrentScene() {
  for (const auto& [id, _] : _entityToScene) {
    _world.destroyEntity(id);
  }
  _entityToScene.clear();
  _currentScenePath.clear();
  _currentSceneHandle = AssetHandle{};
}

void SceneManager::finishTransition() {
  AssetHandle fromHandle;
  AssetHandle toHandle;
  if (_activeTransition.has_value()) {
    fromHandle = _activeTransition->fromHandle;
    toHandle = _activeTransition->toHandle;
  }

  _phase = Phase::Idle;
  _transitionState.active = false;
  _transitionState.progress = 1.0f;
  _activeTransition.reset();

  _eventBus.emit(EVT_SceneTransitionFinished,
                 events::SceneTransitionFinished{fromHandle, toHandle});
}

void SceneManager::refreshTransitionState() {
  if (_activeTransition.has_value()) {
    const auto& t = *_activeTransition;
    _transitionState.active = true;
    _transitionState.progress =
        t.config.duration > 0.0f
            ? std::clamp(t.elapsed / t.config.duration, 0.0f, 1.0f)
            : 1.0f;
    _transitionState.fromScene = t.fromHandle;
    _transitionState.toScene = t.toHandle;
    _transitionState.duration = t.config.duration;
  } else {
    _transitionState.active = false;
  }
}
