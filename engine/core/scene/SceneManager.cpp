#include "SceneManager.hpp"
#include "SceneEvents.hpp"
#include "transitions/SceneTransition.hpp"
#include "transitions/FadeTransition.hpp"
#include "transitions/SlideTransition.hpp"
#include "transitions/CrossFadeTransition.hpp"
#include <glm/glm.hpp>
#include "../assets/RawAsset.hpp"
#include "../assets/AssetHandle.hpp"
#include "../ecs/component/ComponentInfo.hpp"
#include "../ecs/component/ComponentRegistry.hpp"
#include <stdexcept>
#include <nlohmann/json.hpp>
#include <algorithm>

SceneManager::SceneManager(World& world, AssetManager& assets, EventBus& events)
    : _world(world), _assets(assets), _events(events) {
}

SceneManager::~SceneManager() {
    unloadAllScenes();
}

SceneHandle SceneManager::allocateHandle() {
    SceneHandle handle{_nextSceneId++, _sceneGeneration};
    return handle;
}

SceneHandle SceneManager::loadScene(const std::filesystem::path& path) {
    // Get scene name first (for event)
    std::string sceneName = path.filename().string();
    
    // Load JSON asset
    AssetHandle assetHandle = _assets.loadAsset(path);
    const RawAsset& asset = _assets.getRawAsset(assetHandle);
    
    // Parse JSON
    std::string jsonString(asset.data.begin(), asset.data.end());
    nlohmann::json sceneJson = nlohmann::json::parse(jsonString);
    
    // Get scene name from JSON if available
    if (sceneJson.contains("name")) {
        sceneName = sceneJson["name"].get<std::string>();
    }
    
    // Allocate handle
    SceneHandle handle = allocateHandle();
    
    // Emit load started event
    _events.emit(events::SceneLoadStarted{handle, sceneName});
    
    // Create scene
    auto scene = std::make_unique<Scene>(_world, sceneName);
    
    // Load scene (creates root entity)
    scene->onLoad();
    
    // Parse JSON and create entities
    parseSceneJSON(*scene, sceneJson);
    
    // Store scene
    _scenes[handle] = std::move(scene);
    
    // If stack is empty, this becomes the base scene
    if (_sceneStack.empty()) {
        _sceneStack.push_back(handle);
        _activeScene = handle;
    }
    // If stack has scenes, don't automatically push (caller decides)
    // This allows loadScene() to load without activating
    
    // Emit load completed event
    _events.emit(events::SceneLoadCompleted{handle, sceneName});
    
    return handle;
}

void SceneManager::unloadScene(SceneHandle handle) {
    auto it = _scenes.find(handle);
    if (it == _scenes.end()) {
        return;  // Scene not found
    }
    
    Scene* scene = it->second.get();
    std::string sceneName = scene->getName();
    
    // Emit unload started event
    _events.emit(events::SceneUnloadStarted{handle, sceneName});
    
    // Unload scene (destroys all entities)
    scene->onUnload();
    
    // Remove from stack if present
    _sceneStack.erase(
        std::remove(_sceneStack.begin(), _sceneStack.end(), handle),
        _sceneStack.end()
    );
    
    // Remove from map
    _scenes.erase(it);
    
    // Clear active scene if it was active
    if (_activeScene == handle) {
        // Activate new top scene if stack has one
        if (!_sceneStack.empty()) {
            SceneHandle newTop = _sceneStack.back();
            Scene* newTopScene = getScene(newTop);
            if (newTopScene) {
                newTopScene->onResume();
                _activeScene = newTop;
                _events.emit(events::SceneActivated{newTop, newTopScene->getName()});
            } else {
                _activeScene = SceneHandle::invalid();
            }
        } else {
            _activeScene = SceneHandle::invalid();
        }
    }
    
    // Emit unloaded event
    _events.emit(events::SceneUnloaded{handle, sceneName});
}

void SceneManager::unloadAllScenes() {
    // Unload all scenes
    for (auto& [handle, scene] : _scenes) {
        std::string sceneName = scene->getName();
        _events.emit(events::SceneUnloadStarted{handle, sceneName});
        scene->onUnload();
        _events.emit(events::SceneUnloaded{handle, sceneName});
    }
    
    _scenes.clear();
    _sceneStack.clear();
    _activeScene = SceneHandle::invalid();
}

Scene* SceneManager::getScene(SceneHandle handle) {
    auto it = _scenes.find(handle);
    if (it == _scenes.end()) {
        return nullptr;
    }
    return it->second.get();
}

Scene* SceneManager::getActiveScene() {
    if (_sceneStack.empty()) {
        return nullptr;
    }
    SceneHandle topHandle = _sceneStack.back();
    return getScene(topHandle);
}

std::vector<SceneHandle> SceneManager::getLoadedScenes() const {
    std::vector<SceneHandle> handles;
    handles.reserve(_scenes.size());
    for (const auto& [handle, _] : _scenes) {
        handles.push_back(handle);
    }
    return handles;
}

void SceneManager::parseSceneJSON(Scene& scene, const nlohmann::json& sceneJson) {
    if (!sceneJson.contains("entities")) {
        return;  // No entities to create
    }
    
    // Map entity IDs (from JSON "id" field) to EntityIds
    std::unordered_map<std::string, EntityId> idMap;
    
    // First pass: Create all entities and build ID map
    for (const auto& entityJson : sceneJson["entities"]) {
        createEntityFromJSON(scene, entityJson, idMap);
    }
    
    // Second pass: Set parent relationships
    for (const auto& entityJson : sceneJson["entities"]) {
        // Get entity ID from JSON
        if (!entityJson.contains("id")) {
            continue;  // No ID, skip parent relationship
        }
        
        std::string entityId = entityJson["id"].get<std::string>();
        auto it = idMap.find(entityId);
        if (it == idMap.end()) {
            continue;  // Entity not found (shouldn't happen)
        }
        
        EntityId entity = it->second;
        
        // Get parent ID from JSON
        if (entityJson.contains("parent")) {
            auto parentValue = entityJson["parent"];
            
            if (parentValue.is_null()) {
                // Parent is null = root entity (already is, no action needed)
                continue;
            } else if (parentValue.is_string()) {
                // Parent is a string ID reference
                std::string parentId = parentValue.get<std::string>();
                auto parentIt = idMap.find(parentId);
                
                if (parentIt != idMap.end()) {
                    EntityId parent = parentIt->second;
                    scene.reparent(entity, parent);
                } else {
                    // Parent ID not found - log warning but continue
                    // Could be intentional (parent in different scene) or error
                }
            }
        }
    }
}

void SceneManager::createEntityFromJSON(Scene& scene, const nlohmann::json& entityJson,
                                         std::unordered_map<std::string, EntityId>& idMap) {
    // Create entity in scene
    EntityId entity = scene.createEntity();
    
    // Store entity ID mapping if "id" field exists
    if (entityJson.contains("id")) {
        std::string id = entityJson["id"].get<std::string>();
        if (!id.empty()) {
            idMap[id] = entity;
        }
    }
    
    // Add name tag if "name" field exists
    if (entityJson.contains("name")) {
        std::string name = entityJson["name"].get<std::string>();
        _world.addTag(entity, name);
    }
    
    // Add components
    if (entityJson.contains("components")) {
        auto components = entityJson["components"];
        for (const auto& [componentName, componentData] : components.items()) {
            auto maybeId = _world.getRegistry().getComponentIdByName(componentName);
            if (!maybeId.has_value()) {
                continue;  // Component not registered
            }
            
            auto componentId = maybeId.value();
            const auto* info = _world.getRegistry().getInfo(componentId);
            if (info && info->deserializeFn) {
                info->deserializeFn(_world, entity, componentData);
            }
        }
    }
}

SceneHandle SceneManager::loadSceneAdditive(const std::filesystem::path& path) {
    // Load scene (loadScene handles events and basic setup)
    SceneHandle handle = loadScene(path);
    
    // Pause previous top scene if stack already has scenes
    if (!_sceneStack.empty() && _sceneStack.back() != handle) {
        SceneHandle previousTop = _sceneStack.back();
        Scene* previousScene = getScene(previousTop);
        if (previousScene && previousScene->getState() == SceneState::Active) {
            previousScene->onPause();
        }
    }
    
    // Push to stack (if not already there)
    auto it = std::find(_sceneStack.begin(), _sceneStack.end(), handle);
    if (it == _sceneStack.end()) {
        _sceneStack.push_back(handle);
    }
    
    // Activate
    Scene* scene = getScene(handle);
    if (scene && scene->getState() != SceneState::Active) {
        scene->onActivate();
        _events.emit(events::SceneActivated{handle, scene->getName()});
    }
    
    _activeScene = handle;
    
    return handle;
}

SceneHandle SceneManager::pushScene(const std::filesystem::path& path) {
    // Load scene (loadScene will handle events and basic setup)
    SceneHandle handle = loadScene(path);
    
    // Pause previous top scene (if any)
    if (!_sceneStack.empty() && _sceneStack.back() != handle) {
        SceneHandle previousTop = _sceneStack.back();
        Scene* previousScene = getScene(previousTop);
        if (previousScene && previousScene->getState() == SceneState::Active) {
            previousScene->onPause();
        }
    }
    
    // Push to stack (if not already there)
    auto it = std::find(_sceneStack.begin(), _sceneStack.end(), handle);
    if (it == _sceneStack.end()) {
        _sceneStack.push_back(handle);
    }
    
    // Activate new top scene
    Scene* scene = getScene(handle);
    if (scene && scene->getState() != SceneState::Active) {
        scene->onActivate();
        _events.emit(events::SceneActivated{handle, scene->getName()});
    }
    
    // Update active scene
    _activeScene = handle;
    
    return handle;
}

SceneHandle SceneManager::popScene() {
    if (_sceneStack.empty()) {
        return SceneHandle::invalid();
    }
    
    // Get top scene
    SceneHandle topHandle = _sceneStack.back();
    Scene* topScene = getScene(topHandle);
    
    // Deactivate and pop
    if (topScene) {
        topScene->onDeactivate();
        _events.emit(events::SceneDeactivated{topHandle, topScene->getName()});
    }
    
    _sceneStack.pop_back();
    
    // Activate new top scene (if any)
    if (!_sceneStack.empty()) {
        SceneHandle newTop = _sceneStack.back();
        Scene* newTopScene = getScene(newTop);
        if (newTopScene) {
            newTopScene->onResume();  // Resume from pause
            _events.emit(events::SceneActivated{newTop, newTopScene->getName()});
        }
        _activeScene = newTop;
    } else {
        _activeScene = SceneHandle::invalid();
    }
    
    return topHandle;
}

void SceneManager::pauseScene(SceneHandle handle) {
    Scene* scene = getScene(handle);
    if (!scene) {
        return;  // Scene not found
    }
    
    if (scene->getState() != SceneState::Active) {
        // Can only pause active scenes
        return;
    }
    
    scene->onPause();
    
    // If this is the active scene, update active scene
    if (_activeScene == handle) {
        // Active scene is paused - this is unusual but allowed
        // (might want to activate another scene)
    }
}

void SceneManager::resumeScene(SceneHandle handle) {
    Scene* scene = getScene(handle);
    if (!scene) {
        return;
    }
    
    if (scene->getState() != SceneState::Paused) {
        // Can only resume paused scenes
        return;
    }
    
    scene->onResume();
    
    // If resuming the active scene, ensure it's on top of stack
    if (_activeScene == handle) {
        // Already active, just resuming from pause
    }
}

void SceneManager::setActiveScene(SceneHandle handle) {
    Scene* scene = getScene(handle);
    if (!scene) {
        return;  // Scene not found
    }
    
    // Deactivate current active scene
    if (_activeScene.isValid()) {
        Scene* currentActive = getScene(_activeScene);
        if (currentActive && currentActive->getState() == SceneState::Active) {
            currentActive->onDeactivate();
            _events.emit(events::SceneDeactivated{_activeScene, currentActive->getName()});
        }
    }
    
    // Ensure scene is loaded
    if (scene->getState() == SceneState::Unloaded) {
        scene->onLoad();
    }
    
    // Activate new scene
    scene->onActivate();
    
    // Move to top of stack (remove from current position if present, add to top)
    auto it = std::find(_sceneStack.begin(), _sceneStack.end(), handle);
    if (it != _sceneStack.end()) {
        _sceneStack.erase(it);
    }
    _sceneStack.push_back(handle);
    
    // Update active scene
    _activeScene = handle;
    
    // Emit event
    _events.emit(events::SceneActivated{handle, scene->getName()});
}

SceneHandle SceneManager::switchScene(const std::filesystem::path& path) {
    // Unload all current scenes
    unloadAllScenes();
    
    // Clear stack
    _sceneStack.clear();
    
    // Load new scene
    SceneHandle handle = loadScene(path);
    
    // Ensure it's on stack and active
    if (_sceneStack.empty()) {
        _sceneStack.push_back(handle);
    }
    _activeScene = handle;
    
    // Activate
    Scene* scene = getScene(handle);
    if (scene && scene->getState() != SceneState::Active) {
        if (scene->getState() == SceneState::Unloaded) {
            scene->onLoad();
        }
        scene->onActivate();
        _events.emit(events::SceneActivated{handle, scene->getName()});
    }
    
    return handle;
}

void SceneManager::update(float dt) {
    // Process active transition
    if (_activeTransition) {
        processTransition(dt);
    }
    
    // Handle deferred scene operations if needed
    // (e.g., deferred unloads, deferred state changes)
    
    // Update scene timers if needed
    // (e.g., auto-save timers, scene-specific timers)
}

void SceneManager::processTransition(float dt) {
    if (!_activeTransition) {
        return;  // No active transition
    }
    
    // Update transition
    _activeTransition->update(dt);
    
    // Check if complete
    if (_activeTransition->isComplete()) {
        finalizeSceneSwitch();
    }
}

void SceneManager::finalizeSceneSwitch() {
    if (!_activeTransition) {
        return;
    }
    
    // Unload old scene
    if (_transitionFrom.isValid()) {
        Scene* fromScene = getScene(_transitionFrom);
        if (fromScene) {
            fromScene->onDeactivate();
            unloadScene(_transitionFrom);
        }
    }
    
    // Activate new scene
    Scene* toScene = getScene(_transitionTo);
    if (toScene) {
        toScene->onActivate();
        _activeScene = _transitionTo;
        
        // Update stack
        _sceneStack.clear();
        _sceneStack.push_back(_transitionTo);
    }
    
    // Emit transition completed event
    _events.emit(events::SceneTransitionCompleted{_transitionFrom, _transitionTo});
    
    // Clear transition state
    _activeTransition.reset();
    _transitionFrom = SceneHandle::invalid();
    _transitionTo = SceneHandle::invalid();
}

void SceneManager::switchScene(const std::filesystem::path& path, std::unique_ptr<SceneTransition> transition) {
    if (!transition) {
        // No transition, use basic switch
        switchScene(path);
        return;
    }
    
    // Get current active scene
    SceneHandle fromHandle = _activeScene;
    
    // Load new scene (but don't activate yet)
    SceneHandle toHandle = loadScene(path);
    Scene* toScene = getScene(toHandle);
    if (!toScene) {
        return;  // Failed to load
    }
    
    // Don't activate new scene yet - transition will handle it
    if (toScene->getState() == SceneState::Unloaded) {
        toScene->onLoad();
    }
    // Keep it paused during transition
    
    // Store transition state
    _activeTransition = std::move(transition);
    _transitionFrom = fromHandle;
    _transitionTo = toHandle;
    
    // Emit transition started event
    std::string fromName = fromHandle.isValid() && getScene(fromHandle) 
        ? getScene(fromHandle)->getName() : "";
    std::string toName = toScene->getName();
    std::string transitionType = "transition";  // Could be more specific based on transition type
    _events.emit(events::SceneTransitionStarted{fromHandle, toHandle, transitionType});
}

void SceneManager::switchSceneFade(const std::filesystem::path& path, 
                                    float duration, 
                                    glm::vec4 color) {
    auto transition = std::make_unique<FadeTransition>(duration, color);
    switchScene(path, std::move(transition));
}

void SceneManager::switchSceneSlide(const std::filesystem::path& path,
                                     float duration,
                                     SlideDirection direction) {
    auto transition = std::make_unique<SlideTransition>(duration, direction);
    switchScene(path, std::move(transition));
}

void SceneManager::switchSceneCrossFade(const std::filesystem::path& path,
                                        float duration) {
    auto transition = std::make_unique<CrossFadeTransition>(duration);
    switchScene(path, std::move(transition));
}
