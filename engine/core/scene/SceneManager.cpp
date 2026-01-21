#include "SceneManager.hpp"
#include "../assets/RawAsset.hpp"
#include "../assets/AssetHandle.hpp"
#include "../ecs/component/ComponentInfo.hpp"
#include "../ecs/component/ComponentRegistry.hpp"
#include <stdexcept>
#include <nlohmann/json.hpp>

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
    // Load JSON asset
    AssetHandle assetHandle = _assets.loadAsset(path);
    const RawAsset& asset = _assets.getRawAsset(assetHandle);
    
    // Parse JSON
    std::string jsonString(asset.data.begin(), asset.data.end());
    nlohmann::json sceneJson = nlohmann::json::parse(jsonString);
    
    // Get scene name
    std::string sceneName = path.filename().string();
    if (sceneJson.contains("name")) {
        sceneName = sceneJson["name"].get<std::string>();
    }
    
    // Allocate handle and create scene
    SceneHandle handle = allocateHandle();
    auto scene = std::make_unique<Scene>(_world, sceneName);
    
    // Load scene (creates root entity)
    scene->onLoad();
    
    // Parse JSON and create entities
    parseSceneJSON(*scene, sceneJson);
    
    // Store scene
    _scenes[handle] = std::move(scene);
    
    // Set as active (for now, simple - Phase 3 will add scene stack)
    _activeScene = handle;
    
    return handle;
}

void SceneManager::unloadScene(SceneHandle handle) {
    auto it = _scenes.find(handle);
    if (it == _scenes.end()) {
        return;  // Scene not found
    }
    
    // Unload scene (destroys all entities)
    it->second->onUnload();
    
    // Remove from map
    _scenes.erase(it);
    
    // Clear active scene if it was active
    if (_activeScene == handle) {
        _activeScene = SceneHandle::invalid();
    }
}

void SceneManager::unloadAllScenes() {
    // Unload all scenes
    for (auto& [handle, scene] : _scenes) {
        scene->onUnload();
    }
    
    _scenes.clear();
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
    if (!_activeScene.isValid()) {
        return nullptr;
    }
    return getScene(_activeScene);
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
