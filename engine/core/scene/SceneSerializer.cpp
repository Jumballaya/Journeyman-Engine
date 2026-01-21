#include "SceneSerializer.hpp"
#include "SceneGraph.hpp"
#include "../ecs/component/ComponentRegistry.hpp"
#include <algorithm>

SceneSerializer::SceneSerializer(World& world, AssetManager& assets)
    : _world(world), _assets(assets) {
}

nlohmann::json SceneSerializer::serializeScene(const Scene& scene) {
    nlohmann::json sceneJson;
    
    // Serialize scene metadata
    sceneJson["name"] = scene.getName();
    sceneJson["version"] = "1.0";
    
    // Serialize settings (placeholder - can be enhanced later)
    nlohmann::json settings;
    sceneJson["settings"] = settings;
    
    // Serialize entities
    nlohmann::json entitiesJson = nlohmann::json::array();
    
    // Serialize root entities (children of scene root)
    scene.forEachRootEntity([&](EntityId entity) {
        nlohmann::json entityJson = serializeEntity(entity, true);
        if (!entityJson.empty()) {
            entitiesJson.push_back(entityJson);
        }
    });
    
    sceneJson["entities"] = entitiesJson;
    
    return sceneJson;
}

nlohmann::json SceneSerializer::serializeEntity(EntityId entity, bool includeChildren) {
    if (!_world.isAlive(entity)) {
        return nlohmann::json{};
    }
    
    nlohmann::json entityJson;
    
    // Serialize entity ID (use name if available, otherwise generate temp ID)
    std::string entityId = getEntityName(entity);
    if (entityId.empty()) {
        // No name, generate temp ID for this serialization
        entityId = generateTempId();
    }
    entityJson["id"] = entityId;
    
    // If entity has a name tag, include it
    if (!entityId.empty() && entityId != generateTempId()) {
        entityJson["name"] = entityId;
    }
    
    // Serialize parent (if not root)
    EntityId parent = SceneGraph::getParent(_world, entity);
    if (_world.isAlive(parent)) {
        std::string parentName = getEntityName(parent);
        if (!parentName.empty()) {
            entityJson["parent"] = parentName;
        } else {
            entityJson["parent"] = nullptr;  // Parent has no name, can't reference
        }
    } else {
        entityJson["parent"] = nullptr;
    }
    
    // Serialize components
    nlohmann::json componentsJson;
    
    _world.getRegistry().forEachRegisteredComponent([&](ComponentId componentId) {
        const ComponentInfo* info = _world.getRegistry().getInfo(componentId);
        if (!info || !info->serializeFn) {
            return;  // Component has no serializer
        }
        
        // Check if component is saveable
        if (!info->saveable) {
            return;  // Skip non-saveable components
        }
        
        nlohmann::json componentJson = info->serializeFn(_world, entity);
        if (!componentJson.is_null() && !componentJson.empty()) {
            componentsJson[std::string(info->name)] = componentJson;
        }
    });
    
    if (!componentsJson.empty()) {
        entityJson["components"] = componentsJson;
    }
    
    // Note: Children are serialized separately in the main entities array
    // They reference their parent via the "parent" field
    
    return entityJson;
}

nlohmann::json SceneSerializer::serializeComponent(EntityId entity, ComponentId componentId) {
    const ComponentInfo* info = _world.getRegistry().getInfo(componentId);
    if (!info || !info->serializeFn) {
        return nlohmann::json{};  // No serializer
    }
    
    return info->serializeFn(_world, entity);
}

void SceneSerializer::deserializeScene(Scene& scene, const nlohmann::json& data) {
    // Clear ID map
    _idMap.clear();
    _nextTempId = 1;
    
    // Parse scene metadata
    if (data.contains("name")) {
        // Scene name is set in constructor, but could update here if needed
    }
    
    // Deserialize settings (placeholder)
    if (data.contains("settings")) {
        // TODO: Deserialize scene settings
    }
    
    if (!data.contains("entities")) {
        return;  // No entities to load
    }
    
    // First pass: Create all entities and build ID map
    for (const auto& entityJson : data["entities"]) {
        EntityId entity = deserializeEntity(entityJson, scene, scene.getRoot());
        
        // Store in ID map if has ID
        if (entityJson.contains("id")) {
            std::string id = entityJson["id"].get<std::string>();
            _idMap[id] = entity;
        }
    }
    
    // Second pass: Set parent relationships
    for (const auto& entityJson : data["entities"]) {
        if (!entityJson.contains("id")) {
            continue;  // Can't reference entities without IDs
        }
        
        std::string entityId = entityJson["id"].get<std::string>();
        EntityId entity = resolveEntityRef(entityId);
        if (!_world.isAlive(entity)) {
            continue;
        }
        
        // Set parent
        if (entityJson.contains("parent")) {
            auto parentValue = entityJson["parent"];
            if (parentValue.is_null()) {
                // Parent is null = root (already is)
                continue;
            } else if (parentValue.is_string()) {
                std::string parentId = parentValue.get<std::string>();
                EntityId parent = resolveEntityRef(parentId);
                if (_world.isAlive(parent)) {
                    scene.reparent(entity, parent);
                }
            }
        }
    }
}

EntityId SceneSerializer::deserializeEntity(const nlohmann::json& data, Scene& scene, EntityId parent) {
    // Create entity in scene
    EntityId entity = (parent == scene.getRoot() || !_world.isAlive(parent))
        ? scene.createEntity() 
        : scene.createEntity(parent);
    
    // Store in ID map if has ID
    if (data.contains("id")) {
        std::string id = data["id"].get<std::string>();
        _idMap[id] = entity;
    }
    
    // Add name tag if present
    if (data.contains("name")) {
        std::string name = data["name"].get<std::string>();
        _world.addTag(entity, name);
    } else if (data.contains("id")) {
        // Use ID as name if no explicit name
        std::string id = data["id"].get<std::string>();
        _world.addTag(entity, id);
    }
    
    // Deserialize components
    if (data.contains("components")) {
        auto components = data["components"];
        for (const auto& [componentName, componentData] : components.items()) {
            deserializeComponent(entity, componentName, componentData);
        }
    }
    
    return entity;
}

void SceneSerializer::deserializeComponent(EntityId entity, const std::string& componentName, const nlohmann::json& data) {
    auto maybeComponentId = _world.getRegistry().getComponentIdByName(componentName);
    if (!maybeComponentId.has_value()) {
        return;  // Component not registered
    }
    
    ComponentId componentId = maybeComponentId.value();
    const ComponentInfo* info = _world.getRegistry().getInfo(componentId);
    if (!info || !info->deserializeFn) {
        return;  // No deserializer
    }
    
    info->deserializeFn(_world, entity, data);
}

EntityId SceneSerializer::resolveEntityRef(const std::string& ref) {
    if (ref.empty()) {
        return EntityId{0, 0};  // Invalid
    }
    
    auto it = _idMap.find(ref);
    if (it == _idMap.end()) {
        return EntityId{0, 0};  // Not found
    }
    
    return it->second;
}

std::string SceneSerializer::generateTempId() {
    return "_temp_" + std::to_string(_nextTempId++);
}

std::string SceneSerializer::getEntityName(EntityId entity) const {
    // Since TagSymbol is a hash, we can't reverse it to get the original string.
    // For now, we'll use a simple approach: check if entity has any tags,
    // and if we're serializing, we'll use the ID from the map if available.
    // 
    // A better long-term solution would be to maintain a reverse mapping
    // (TagSymbol -> string) or use a NameComponent.
    //
    // For Phase 5, we'll use a workaround: during deserialization, we store
    // the name in _idMap. During serialization, we can check if the entity
    // is in the reverse map.
    
    // Check if this entity is in our ID map (reverse lookup)
    for (const auto& [name, id] : _idMap) {
        if (id == entity) {
            return name;
        }
    }
    
    // Entity not in map - no name available
    // In a full implementation, we'd need to track names separately
    return "";
}

std::vector<EntityId> SceneSerializer::getEntityChildren(EntityId entity) const {
    return SceneGraph::getChildren(_world, entity);
}
