#pragma once

#include <string>
#include <unordered_set>
#include <functional>
#include <nlohmann/json.hpp>

#include "../ecs/World.hpp"
#include "../ecs/entity/EntityId.hpp"
#include "SceneHandle.hpp"

/**
 * Represents the current state of a scene.
 */
enum class SceneState {
    Unloaded,   // Scene not loaded, no entities created
    Loading,    // Scene is currently being loaded
    Active,     // Scene is active and updating
    Paused,     // Scene is paused (entities exist but not updating)
    Unloading   // Scene is being unloaded
};

/**
 * Scene manages a collection of entities within a scene context.
 * Each scene has an invisible root entity that all scene entities
 * are children of (by default).
 */
class Scene {
public:
    /**
     * Creates a new scene with the given name.
     * @param world - Reference to the ECS World
     * @param name - Name of the scene
     */
    explicit Scene(World& world, const std::string& name);
    
    /**
     * Destructor - unloads scene if still loaded.
     */
    ~Scene();
    
    // Non-copyable (scenes are unique)
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    
    // Movable
    Scene(Scene&&) noexcept = default;
    Scene& operator=(Scene&&) noexcept = default;
    
    // ====================================================================
    // Lifecycle Methods
    // ====================================================================
    
    /**
     * Called when scene is being loaded.
     * Creates the root entity and sets state to Loading.
     */
    void onLoad();
    
    /**
     * Called when scene is being unloaded.
     * Destroys all entities and sets state to Unloaded.
     */
    void onUnload();
    
    /**
     * Called when scene becomes active.
     * Sets state to Active.
     */
    void onActivate();
    
    /**
     * Called when scene becomes inactive.
     * Sets state to Paused.
     */
    void onDeactivate();
    
    /**
     * Called when scene is paused.
     * Sets state to Paused.
     */
    void onPause();
    
    /**
     * Called when scene is resumed from pause.
     * Sets state to Active.
     */
    void onResume();
    
    // ====================================================================
    // Entity Management
    // ====================================================================
    
    /**
     * Creates a new entity in this scene.
     * Entity is automatically a child of the scene root.
     * @returns EntityId of the created entity
     */
    EntityId createEntity();
    
    /**
     * Creates a new entity with a parent.
     * @param parent - Parent entity (must be in this scene)
     * @returns EntityId of the created entity
     */
    EntityId createEntity(EntityId parent);
    
    /**
     * Destroys an entity in this scene.
     * Also removes it from tracking.
     * @param id - Entity to destroy
     */
    void destroyEntity(EntityId id);
    
    /**
     * Reparents an entity within the scene.
     * @param entity - Entity to reparent
     * @param newParent - New parent (or invalid EntityId for root)
     */
    void reparent(EntityId entity, EntityId newParent);
    
    // ====================================================================
    // Queries
    // ====================================================================
    
    /**
     * Gets the invisible root entity of this scene.
     */
    EntityId getRoot() const { return _root; }
    
    /**
     * Gets the name of this scene.
     */
    const std::string& getName() const { return _name; }
    
    /**
     * Gets the current state of this scene.
     */
    SceneState getState() const { return _state; }
    
    /**
     * Checks if an entity belongs to this scene.
     */
    bool hasEntity(EntityId id) const {
        return _entities.find(id) != _entities.end();
    }
    
    /**
     * Gets the number of entities in this scene (excluding root).
     */
    size_t getEntityCount() const {
        return _entities.size();
    }
    
    // ====================================================================
    // Iteration
    // ====================================================================
    
    /**
     * Iterates over all entities in the scene (excluding root).
     * @param fn - Function called for each entity: fn(EntityId)
     */
    template<typename Fn>
    void forEachEntity(Fn&& fn) {
        for (EntityId id : _entities) {
            if (_world.isAlive(id)) {
                fn(id);
            }
        }
    }
    
    /**
     * Iterates over root entities (direct children of scene root).
     * @param fn - Function called for each root entity: fn(EntityId)
     */
    template<typename Fn>
    void forEachRootEntity(Fn&& fn) {
        for (EntityId id : _rootEntities) {
            if (_world.isAlive(id)) {
                fn(id);
            }
        }
    }
    
    // ====================================================================
    // Serialization (Basic - Full implementation in Phase 5)
    // ====================================================================
    
    /**
     * Serializes the scene to JSON.
     * @returns JSON representation of the scene
     */
    nlohmann::json serialize() const;
    
    /**
     * Deserializes the scene from JSON.
     * @param data - JSON data to load
     */
    void deserialize(const nlohmann::json& data);
    
private:
    World& _world;                              // Reference to ECS world
    std::string _name;                          // Scene name
    SceneState _state{SceneState::Unloaded};    // Current state
    EntityId _root{0, 0};                       // Invisible root entity
    std::unordered_set<EntityId> _entities;     // All entities in scene
    std::unordered_set<EntityId> _rootEntities; // Direct children of root
    
    /**
     * Adds an entity to the scene tracking.
     * Called internally when creating entities.
     */
    void addEntity(EntityId id);
    
    /**
     * Removes an entity from scene tracking.
     * Called internally when destroying entities.
     */
    void removeEntity(EntityId id);
};
