#pragma once

#include <filesystem>
#include <unordered_map>
#include <memory>
#include <string>
#include <vector>

#include "../assets/AssetManager.hpp"
#include "../ecs/World.hpp"
#include "../events/EventBus.hpp"
#include "Scene.hpp"
#include "SceneHandle.hpp"

/**
 * SceneManager manages scene loading, unloading, and state.
 * Basic implementation for Phase 2 - extended in Phase 3.
 */
class SceneManager {
public:
    /**
     * Creates a SceneManager.
     * @param world - Reference to ECS World
     * @param assets - Reference to AssetManager
     * @param events - Reference to EventBus (for future event integration)
     */
    SceneManager(World& world, AssetManager& assets, EventBus& events);
    
    /**
     * Destructor - unloads all scenes.
     */
    ~SceneManager();
    
    // Non-copyable
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;
    
    // ====================================================================
    // Scene Loading
    // ====================================================================
    
    /**
     * Loads a scene from a JSON file.
     * Creates a new Scene instance and parses entities.
     * @param path - Path to scene JSON file
     * @returns SceneHandle for the loaded scene
     */
    SceneHandle loadScene(const std::filesystem::path& path);
    
    /**
     * Unloads a scene.
     * Destroys all entities and removes scene from tracking.
     * @param handle - Handle of scene to unload
     */
    void unloadScene(SceneHandle handle);
    
    /**
     * Unloads all scenes.
     */
    void unloadAllScenes();
    
    // ====================================================================
    // Scene Queries
    // ====================================================================
    
    /**
     * Gets a scene by handle.
     * @param handle - Scene handle
     * @returns Pointer to Scene, or nullptr if not found
     */
    Scene* getScene(SceneHandle handle);
    
    /**
     * Gets the currently active scene.
     * @returns Pointer to active scene, or nullptr if none
     */
    Scene* getActiveScene();
    
    /**
     * Gets the handle of the active scene.
     * @returns Active scene handle, or invalid if none
     */
    SceneHandle getActiveSceneHandle() const {
        return _activeScene;
    }
    
    /**
     * Gets all loaded scene handles.
     * @returns Vector of scene handles
     */
    std::vector<SceneHandle> getLoadedScenes() const;
    
private:
    World& _world;
    AssetManager& _assets;
    EventBus& _events;
    
    std::unordered_map<SceneHandle, std::unique_ptr<Scene>> _scenes;
    SceneHandle _activeScene{0, 0};  // Invalid = no active scene
    
    uint32_t _nextSceneId{1};
    uint32_t _sceneGeneration{0};
    
    /**
     * Allocates a new scene handle.
     */
    SceneHandle allocateHandle();
    
    /**
     * Parses a scene JSON and creates entities in the scene.
     * Migrated from SceneLoader functionality.
     */
    void parseSceneJSON(Scene& scene, const nlohmann::json& sceneJson);
    
    /**
     * Creates an entity from JSON data.
     * Migrated from SceneLoader functionality.
     */
    void createEntityFromJSON(Scene& scene, const nlohmann::json& entityJson, 
                              std::unordered_map<std::string, EntityId>& idMap);
};
