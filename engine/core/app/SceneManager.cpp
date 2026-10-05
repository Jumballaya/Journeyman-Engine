#include "SceneManager.hpp"

#include <algorithm>
#include <utility>

#include "../logger/logging.hpp"
#include "ApplicationEvents.hpp"

SceneManager::SceneManager(World& world, AssetManager& assetManager, EventBus& eventBus)
    : _world(world), _assetManager(assetManager), _eventBus(eventBus), _loader(world, assetManager) {}

void SceneManager::loadScene(const std::filesystem::path& scenePath) {
  if (_transition) {
    JM_LOG_WARN("[SceneManager] load of '{}' ignored: a transition is running", scenePath.string());
    return;
  }
  replaceScene(scenePath);
  JM_LOG_INFO("[SceneManager] loaded '{}'", _currentScenePath);
}

void SceneManager::transitionTo(const std::filesystem::path& scenePath, TransitionConfig config) {
  if (_transition) {
    JM_LOG_WARN("[SceneManager] transition to '{}' ignored: a transition is running", scenePath.string());
    return;
  }
  const AssetHandle from = _currentSceneHandle;
  const AssetHandle to = replaceScene(scenePath);
  JM_LOG_INFO("[SceneManager] transitioning to '{}' ({:.2f}s{}{})", _currentScenePath, config.duration,
              config.shader.empty() ? "" : ", ", config.shader);

  _transition = ActiveTransition{from, to, config, 0.0f};
  for (auto& l : _transitionListeners) {
    if (l.onBegin) l.onBegin(config);
  }
  _eventBus.emit(EVT_SceneTransitionStarted, events::SceneTransitionStarted{from, to, config.duration});
  if (config.duration <= 0.0f) finishTransition();
}

AssetHandle SceneManager::replaceScene(const std::filesystem::path& scenePath) {
  // Resolve first: a bad path throws before the current scene is touched.
  const AssetHandle handle = _assetManager.loadAsset(scenePath);

  if (_currentSceneHandle.isValid()) {
    _eventBus.emit(EVT_SceneUnloading, events::SceneUnloading{_currentSceneHandle});
    unloadCurrentScene();
  }

  std::vector<EntityId> created;
  try {
    created = _loader.loadScene(handle);  // rolls back its own entities on failure
  } catch (...) {
    _eventBus.emit(EVT_SceneLoadFailed, events::SceneLoadFailed{handle});
    throw;
  }
  for (EntityId id : created) _entityToScene[id] = scenePath.string();
  _currentScenePath = scenePath.string();
  _currentSceneHandle = handle;
  _eventBus.emit(EVT_SceneLoaded, events::SceneLoaded{handle});
  return handle;
}

void SceneManager::tick(float dt) {
  std::optional<Request> request;
  if (!_transition) {
    std::lock_guard lock(_requestMutex);
    request.swap(_request);
  }
  if (request) {
    if (request->transition) {
      transitionTo(request->path, request->config);
    } else {
      loadScene(request->path);
    }
  }

  if (!_transition) return;
  _transition->elapsed += dt;
  const float progress = std::clamp(_transition->elapsed / _transition->config.duration, 0.0f, 1.0f);
  for (auto& l : _transitionListeners) {
    if (l.onProgress) l.onProgress(progress);
  }
  if (progress >= 1.0f) finishTransition();
}

void SceneManager::requestLoad(std::filesystem::path scenePath) {
  std::lock_guard lock(_requestMutex);
  _request = Request{false, std::move(scenePath), {}};
}

void SceneManager::requestTransition(std::filesystem::path scenePath, TransitionConfig config) {
  std::lock_guard lock(_requestMutex);
  _request = Request{true, std::move(scenePath), std::move(config)};
}

void SceneManager::adoptEntity(EntityId id) {
  if (_world.isAlive(id)) _entityToScene[id] = _currentScenePath;
}

void SceneManager::destroyEntity(EntityId id) {
  _entityToScene.erase(id);
  _world.destroyEntity(id);
}

void SceneManager::unloadCurrentScene() {
  for (auto& listener : _unloadListeners) listener();
  for (const auto& [id, _] : _entityToScene) {
    try {
      _world.destroyEntity(id);
    } catch (const std::exception& e) {
      JM_LOG_ERROR("[SceneManager] destroying an entity threw during unload: {}", e.what());
    }
  }
  _entityToScene.clear();
  _currentScenePath.clear();
  _currentSceneHandle = AssetHandle{};
}

void SceneManager::finishTransition() {
  const ActiveTransition done = *_transition;
  _transition.reset();
  for (auto& l : _transitionListeners) {
    if (l.onEnd) l.onEnd();
  }
  _eventBus.emit(EVT_SceneTransitionFinished, events::SceneTransitionFinished{done.from, done.to});
}
