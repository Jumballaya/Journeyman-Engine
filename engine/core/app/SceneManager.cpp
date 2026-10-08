#include "SceneManager.hpp"

#include <algorithm>
#include <utility>

#include "../logger/logging.hpp"
#include "ApplicationEvents.hpp"

SceneManager::SceneManager(World& world, AssetManager& assetManager, EventBus& eventBus)
    : _world(world), _assetManager(assetManager), _eventBus(eventBus), _loader(world, assetManager) {}

void SceneManager::loadScene(const std::filesystem::path& scenePath) { changeScene(scenePath, std::nullopt); }

void SceneManager::transitionTo(const std::filesystem::path& scenePath, TransitionConfig config) {
  changeScene(scenePath, std::move(config));
}

void SceneManager::changeScene(const std::filesystem::path& scenePath, std::optional<TransitionConfig> transition) {
  if (_transition) {
    JM_LOG_WARN("[SceneManager] change to '{}' ignored: a transition is running", scenePath.string());
    return;
  }
  // Resolve first: a bad path throws before the current scene is touched.
  const AssetHandle to = _assetManager.loadAsset(scenePath);
  const AssetHandle from = _currentSceneHandle;
  if (from.isValid()) unload();

  std::vector<EntityId> created;
  try {
    created = _loader.loadScene(to);  // rolls back its own entities on failure
  } catch (...) {
    _eventBus.emit(EVT_SceneLoadFailed, events::SceneLoadFailed{to});
    throw;
  }
  _sceneEntities.insert(created.begin(), created.end());
  _currentScenePath = scenePath.string();
  _currentSceneHandle = to;
  _eventBus.emit(EVT_SceneLoaded, events::SceneLoaded{to});

  if (!transition) {
    JM_LOG_INFO("[SceneManager] loaded '{}'", _currentScenePath);
    return;
  }
  const TransitionConfig& config = *transition;
  JM_LOG_INFO("[SceneManager] transitioning to '{}' ({:.2f}s{}{})", _currentScenePath, config.duration,
              config.shader.empty() ? "" : ", ", config.shader);
  _transition = ActiveTransition{from, to, config, 0.0f};
  for (auto& l : _transitionListeners) {
    if (l.onBegin) l.onBegin(config);
  }
  _eventBus.emit(EVT_SceneTransitionStarted, events::SceneTransitionStarted{from, to, config.duration});
  if (!(config.duration > 0.0f)) finishTransition();  // NaN too: it would never finish
}

void SceneManager::tick(float dt) {
  std::optional<Request> request;
  std::vector<std::pair<std::string, bool>> groups;
  {
    if (!_transition) request.swap(_request);
    groups.swap(_groupRequests);
  }
  // Group changes belong to the scene that asked; a scene change drops them.
  if (request) {
    try {
      changeScene(request->path, std::move(request->transition));
    } catch (const std::exception& e) {
      JM_REPORT_ERROR((ErrorSource{request->path.generic_string()}), "[SceneManager] can't load '{}': {}", request->path.string(), e.what());
    }
  } else {
    for (const auto& [group, spawn] : groups) spawn ? spawnGroup(group) : despawnGroup(group);
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
  _request = Request{std::move(scenePath), std::nullopt};
}

void SceneManager::requestTransition(std::filesystem::path scenePath, TransitionConfig config) {
  _request = Request{std::move(scenePath), std::move(config)};
}

void SceneManager::adoptEntity(EntityId id) {
  if (_world.isAlive(id)) _sceneEntities.insert(id);
}

void SceneManager::destroyEntity(EntityId id) {
  _sceneEntities.erase(id);
  _world.destroyEntity(id);
}

EntityId SceneManager::spawn(const nlohmann::json& entityJson) {
  const EntityId id = _loader.createEntityFromJson(entityJson);
  _sceneEntities.insert(id);
  return id;
}

void SceneManager::spawnGroup(const std::string& group) {
  if (_spawnedGroups.contains(group)) return;
  auto& ids = _spawnedGroups[group];
  auto entries = _loader.groups().find(group);
  if (entries == _loader.groups().end()) {
    // Not an error: a group nothing was placed in yet (an empty room) is simply empty.
    JM_LOG_DEBUG("[SceneManager] scene '{}' has no group '{}'", _currentScenePath, group);
    return;
  }
  for (const auto& entry : entries->second) {
    if (!_loader.conditionsHold(entry)) continue;
    try {
      ids.push_back(spawn(entry));
    } catch (const std::exception& e) {
      JM_LOG_ERROR("[SceneManager] group '{}' entry '{}' failed: {}", group, entry.value("name", std::string()), e.what());
    }
  }
}

void SceneManager::despawnGroup(const std::string& group) {
  auto it = _spawnedGroups.find(group);
  if (it == _spawnedGroups.end()) return;
  for (EntityId id : it->second) destroyEntity(id);
  _spawnedGroups.erase(it);
}

bool SceneManager::groupSpawned(const std::string& group) const { return _spawnedGroups.contains(group); }

void SceneManager::requestGroup(std::string group, bool spawn) {
  _groupRequests.emplace_back(std::move(group), spawn);
}

void SceneManager::unload() {
  if (_currentSceneHandle.isValid()) _eventBus.emit(EVT_SceneUnloading, events::SceneUnloading{_currentSceneHandle});
  for (auto& listener : _unloadListeners) listener();
  // World::destroyEntity isolates throwing destroy hooks.
  for (EntityId id : std::exchange(_sceneEntities, {})) _world.destroyEntity(id);
  _spawnedGroups.clear();
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
