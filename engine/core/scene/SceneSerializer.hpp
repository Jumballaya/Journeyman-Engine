#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

#include "../assets/AssetManager.hpp"
#include "../ecs/World.hpp"
#include "../ecs/entity/EntityId.hpp"
#include "Scene.hpp"

/**
 * SceneSerializer handles serialization and deserialization of scenes.
 * Supports entity hierarchy, component serialization, and entity references.
 */
class SceneSerializer {
public:
    /**
     * Creates a SceneSerializer.
     * @param world - Reference to ECS World
     * @param assets - Reference to AssetManager
     */
    explicit SceneSerializer(World& world, AssetManager& assets);
    
    // ====================================================================
    // Full Scene Serialization
    // ====================================================================
    
    /**
     * Serializes an entire scene to JSON.
     * Includes all entities, components, and hierarchy.
     * 
     * @param scene - Scene to serialize
     * @returns JSON representation of the scene
     */
    nlohmann::json serializeScene(const Scene& scene);
    
    /**
     * Deserializes a scene from JSON.
     * Creates all entities, components, and hierarchy.
     * 
     * @param scene - Scene to populate
     * @param data - JSON data to load
     * @param sceneHandle - Scene handle for entity scene tracking (optional)
     */
    void deserializeScene(Scene& scene, const nlohmann::json& data, SceneHandle sceneHandle = SceneHandle{});
    
    // ====================================================================
    // Single Entity Serialization
    // ====================================================================
    
    /**
     * Serializes a single entity and optionally its children.
     * 
     * @param entity - Entity to serialize
     * @param includeChildren - If true, recursively serialize children
     * @returns JSON representation of the entity
     */
    nlohmann::json serializeEntity(EntityId entity, bool includeChildren = true);
    
    /**
     * Deserializes a single entity from JSON.
     * 
     * @param data - JSON data for the entity
     * @param scene - Scene to add entity to
     * @param parent - Parent entity (or invalid for root)
     * @param sceneHandle - Scene handle for entity scene tracking (optional)
     * @returns EntityId of created entity
     */
    EntityId deserializeEntity(const nlohmann::json& data, Scene& scene, EntityId parent = EntityId{0, 0}, SceneHandle sceneHandle = SceneHandle{});
    
    // ====================================================================
    // Component Serialization
    // ====================================================================
    
    /**
     * Serializes a component from an entity.
     * Uses registered serializer function.
     * 
     * @param entity - Entity with the component
     * @param componentId - Component ID to serialize
     * @returns JSON representation, or null if component doesn't exist
     */
    nlohmann::json serializeComponent(EntityId entity, ComponentId componentId);
    
    /**
     * Deserializes a component to an entity.
     * Uses registered deserializer function.
     * 
     * @param entity - Entity to add component to
     * @param componentName - Name of component type
     * @param data - JSON data for the component
     */
    void deserializeComponent(EntityId entity, const std::string& componentName, const nlohmann::json& data);
    
private:
    World& _world;
    AssetManager& _assets;
    
    // Entity ID mapping for references (used during deserialization)
    std::unordered_map<std::string, EntityId> _idMap;
    
    // Entity ID generation (for entities without IDs in JSON)
    uint32_t _nextTempId{1};
    
    /**
     * Resolves an entity reference (string ID) to EntityId.
     * 
     * @param ref - Entity ID string (or empty for invalid)
     * @returns EntityId, or invalid EntityId if not found
     */
    EntityId resolveEntityRef(const std::string& ref);
    
    /**
     * Generates a temporary entity ID for entities without IDs.
     */
    std::string generateTempId();
    
    /**
     * Gets entity name from tags (if any).
     * Uses convention: entity name is stored as a tag with the same value.
     * Since we can't reverse hash tags, we'll use a simpler approach:
     * entities are referenced by their JSON "id" field, and the "name" tag
     * is set during deserialization.
     */
    std::string getEntityName(EntityId entity) const;
    
    /**
     * Gets all children of an entity.
     */
    std::vector<EntityId> getEntityChildren(EntityId entity) const;
};
