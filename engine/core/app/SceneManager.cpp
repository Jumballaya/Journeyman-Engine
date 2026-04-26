#include "SceneManager.hpp"

#include <vector>

#include "../logger/logging.hpp"
#include "ApplicationEvents.hpp"

SceneManager::SceneManager(World& world, AssetManager& assetManager,
                           EventBus& eventBus)
    : _world(world), _assetManager(assetManager), _eventBus(eventBus),
      _loader(world, assetManager) {}

void SceneManager::loadScene(const std::filesystem::path& scenePath) {
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
  (void)scenePath;
  (void)config;
  JM_LOG_WARN("[SceneManager] transitionTo not implemented (D.4)");
}

void SceneManager::tick(float dt) {
  (void)dt;
  if (_phase == Phase::Idle) {
    return;
  }
  // D.4 fills in the transition advance.
}

void SceneManager::unloadCurrentScene() {
  for (const auto& [id, _] : _entityToScene) {
    _world.destroyEntity(id);
  }
  _entityToScene.clear();
  _currentScenePath.clear();
  _currentSceneHandle = AssetHandle{};
}
