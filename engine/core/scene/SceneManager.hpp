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
#include <glm/glm.hpp>

// Forward declaration
class SceneTransition;

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
    
    /**
     * Loads a scene additively (without unloading current scenes).
     * Scene is added to the stack and becomes active.
     * 
     * @param path - Path to scene JSON file
     * @returns SceneHandle for the loaded scene
     */
    SceneHandle loadSceneAdditive(const std::filesystem::path& path);
    
    // ====================================================================
    // Scene Stack
    // ====================================================================
    
    /**
     * Pushes a scene onto the stack, making it active.
     * The previous top scene (if any) is paused.
     * 
     * @param path - Path to scene JSON file
     * @returns SceneHandle for the pushed scene
     */
    SceneHandle pushScene(const std::filesystem::path& path);
    
    /**
     * Pops the top scene from the stack.
     * The scene below (if any) becomes active.
     * 
     * @returns SceneHandle of the popped scene (invalid if stack was empty)
     */
    SceneHandle popScene();
    
    // ====================================================================
    // Scene State Management
    // ====================================================================
    
    /**
     * Pauses a scene.
     * Scene must be Active.
     * 
     * @param handle - Scene to pause
     */
    void pauseScene(SceneHandle handle);
    
    /**
     * Resumes a paused scene.
     * Scene must be Paused.
     * 
     * @param handle - Scene to resume
     */
    void resumeScene(SceneHandle handle);
    
    /**
     * Sets the active scene.
     * Previous active scene is deactivated.
     * New scene is activated and moved to top of stack.
     * 
     * @param handle - Scene to make active
     */
    void setActiveScene(SceneHandle handle);
    
    // ====================================================================
    // Scene Switching
    // ====================================================================
    
    /**
     * Switches to a new scene, unloading the current scene.
     * Simple version without transition.
     * 
     * @param path - Path to new scene JSON file
     * @returns SceneHandle for the new scene
     */
    SceneHandle switchScene(const std::filesystem::path& path);
    
    /**
     * Switches to a new scene with a transition.
     * 
     * @param path - Path to new scene JSON file
     * @param transition - Transition to use
     */
    void switchScene(const std::filesystem::path& path, std::unique_ptr<SceneTransition> transition);
    
    /**
     * Switches to a new scene with a fade transition.
     * @param path - Path to new scene
     * @param duration - Transition duration in seconds
     * @param color - Fade color (default: black)
     */
    void switchSceneFade(const std::filesystem::path& path, 
                         float duration = 0.5f, 
                         glm::vec4 color = {0.0f, 0.0f, 0.0f, 1.0f});
    
    /**
     * Switches to a new scene with a slide transition.
     * @param path - Path to new scene
     * @param duration - Transition duration in seconds
     * @param direction - Slide direction
     */
    void switchSceneSlide(const std::filesystem::path& path,
                          float duration = 0.5f,
                          SlideDirection direction = SlideDirection::Left);
    
    /**
     * Switches to a new scene with a crossfade transition.
     * @param path - Path to new scene
     * @param duration - Transition duration in seconds
     */
    void switchSceneCrossFade(const std::filesystem::path& path,
                              float duration = 0.5f);
    
    /**
     * Gets the active transition (if any).
     * @returns Pointer to active transition, or nullptr
     */
    SceneTransition* getActiveTransition() const {
        return _activeTransition.get();
    }
    
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
    
    // ====================================================================
    // Update Loop
    // ====================================================================
    
    /**
     * Updates scene manager (call every frame).
     * Handles scene transitions, deferred operations, etc.
     * 
     * @param dt - Delta time since last frame (seconds)
     */
    void update(float dt);
    
private:
    World& _world;
    AssetManager& _assets;
    EventBus& _events;
    
    std::unordered_map<SceneHandle, std::unique_ptr<Scene>> _scenes;
    SceneHandle _activeScene{0, 0};  // Invalid = no active scene
    
    /**
     * Scene stack for layered scenes.
     * Bottom (index 0) = main/base scene
     * Top (last index) = active overlay scene
     * Only top scene is active, others are paused or background.
     */
    std::vector<SceneHandle> _sceneStack;
    
    uint32_t _nextSceneId{1};
    uint32_t _sceneGeneration{0};
    
    // Transition state
    std::unique_ptr<SceneTransition> _activeTransition;
    SceneHandle _transitionFrom;  // Scene transitioning from
    SceneHandle _transitionTo;    // Scene transitioning to
    
    /**
     * Processes the active transition (call from update()).
     */
    void processTransition(float dt);
    
    /**
     * Finalizes the scene switch after transition completes.
     */
    void finalizeSceneSwitch();
    
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
                              std::unordered_map<std::string, EntityId>& idMap,
                              SceneHandle sceneHandle = SceneHandle{});
};
