# Phase 5: Serialization & Save System - Comprehensive Implementation Breakdown

## Overview

Phase 5 implements complete scene serialization and a save/load game system. This phase includes:
1. **Component Serialization Registration** - Add serialize functions to component registration
2. **SceneSerializer** - Full scene serialization/deserialization with hierarchy support
3. **Enhanced Scene JSON Format** - Support for scene settings, version, and metadata
4. **SaveData Structure** - Save file format with metadata and game state
5. **SaveManager** - Complete save/load system with slots, persistent state, and auto-save
6. **Saveable Component Marker** - System to mark which components should be saved

**Goal:** Enable saving and loading game state, with support for scene serialization, persistent cross-scene data, and a complete save slot management system.

**Success Criteria:**
- ✅ Components can be serialized to JSON
- ✅ Scenes can be serialized and deserialized correctly
- ✅ Entity hierarchy is preserved in serialization
- ✅ Save/load system works with multiple save slots
- ✅ Persistent state survives scene transitions
- ✅ Auto-save functionality works
- ✅ Save files can be loaded and restore exact game state

**Prerequisites:**
- ✅ Phase 1 complete (Transform2D, TransformSystem)
- ✅ Phase 2 complete (Scene, SceneManager basic)
- ✅ Phase 3 complete (SceneManager with stack, events)
- ✅ nlohmann/json library functional
- ✅ File system access functional

---

## Step 5.1: Component Serialization Registration

### Objective
Extend the component registration system to support serialization functions, enabling components to be saved to and loaded from JSON.

### Design Concept

**Current State:**
- Components have `deserializeFn` for loading from JSON
- Components are registered with `World::registerComponent(deserializer)`

**New State:**
- Components also have `serializeFn` for saving to JSON
- Components are registered with both serializer and deserializer
- Serialization uses registered functions (same pattern as deserialization)

### Detailed Implementation Steps

#### Step 5.1.1: Update ComponentInfo Structure

**File:** `engine/core/ecs/component/ComponentInfo.hpp`

Update ComponentInfo to include serialize function:
```cpp
#pragma once

#include <cstddef>
#include <functional>
#include <nlohmann/json.hpp>
#include <string_view>

#include "../entity/EntityId.hpp"
#include "ComponentId.hpp"

class World;

struct ComponentInfo {
    std::string_view name;
    size_t size;
    ComponentId id;
    
    // Deserialization function (existing)
    std::function<void(World&, EntityId, const nlohmann::json&)> deserializeFn;
    
    // Serialization function (NEW)
    // Returns JSON representation of component, or null if component doesn't exist
    std::function<nlohmann::json(World&, EntityId)> serializeFn;

    explicit operator bool() const {
        return !name.empty();
    }
};
```

**Action Items:**
- [ ] Update ComponentInfo to include serializeFn
- [ ] Make serializeFn optional (can be nullptr for components that don't serialize)
- [ ] Verify compilation

#### Step 5.1.2: Update ComponentRegistry

**File:** `engine/core/ecs/component/ComponentRegistry.hpp`

Update registerComponent to accept serializer:
```cpp
class ComponentRegistry {
public:
    /**
     * Registers a component type with both serializer and deserializer.
     * 
     * @param name - Component name
     * @param deserializer - Function to deserialize component from JSON
     * @param serializer - Function to serialize component to JSON (can be nullptr)
     */
    template <ComponentType T>
    void registerComponent(
        std::string_view name,
        std::function<void(World&, EntityId, const nlohmann::json&)> deserializer,
        std::function<nlohmann::json(World&, EntityId)> serializer = nullptr
    ) {
        ComponentId id = Component<T>::typeId();

        if (_components.size() <= id) {
            _components.resize(id + 1);
            _components[id] = ComponentInfo{
                name, 
                sizeof(T), 
                id, 
                std::move(deserializer),
                std::move(serializer)  // NEW
            };
            _nameToId[std::string(name)] = id;
        } else {
            // Update existing registration
            _components[id].deserializeFn = std::move(deserializer);
            _components[id].serializeFn = std::move(serializer);  // NEW
        }
    }
    
    // ... rest of class ...
};
```

**Action Items:**
- [ ] Update ComponentRegistry::registerComponent() signature
- [ ] Store serializeFn in ComponentInfo
- [ ] Make serializer optional (default nullptr)
- [ ] Verify compilation

#### Step 5.1.3: Update World::registerComponent()

**File:** `engine/core/ecs/World.hpp`

Update method signature:
```cpp
class World {
    // ... existing methods ...
    
    /**
     * Registers a component type with serializer and deserializer.
     * 
     * @param deserializer - Function to deserialize from JSON
     * @param serializer - Function to serialize to JSON (optional)
     */
    template <ComponentType T>
    void registerComponent(
        std::function<void(World&, EntityId, const nlohmann::json&)> deserializer,
        std::function<nlohmann::json(World&, EntityId)> serializer = nullptr
    );
    
    // ... rest of class ...
};
```

**File:** `engine/core/ecs/World.cpp`

Update implementation:
```cpp
template <ComponentType T>
void World::registerComponent(
    std::function<void(World&, EntityId, const nlohmann::json&)> deserializer,
    std::function<nlohmann::json(World&, EntityId)> serializer
) {
    this->_componentManager.registerStorage<T>();
    this->_registry.registerComponent<T>(T::name(), std::move(deserializer), std::move(serializer));
}
```

**Action Items:**
- [ ] Update World::registerComponent() signature
- [ ] Pass serializer to ComponentRegistry
- [ ] Update all existing registerComponent() calls (add nullptr for serializer)
- [ ] Verify compilation

#### Step 5.1.4: Add Serializers to Existing Components

**File:** `engine/core/app/Application.cpp`

Update Transform2D registration (from Phase 1):
```cpp
void Application::registerTransformComponent() {
    _ecsWorld.registerComponent<Transform2D>(
        // Deserializer (existing)
        [](World& world, EntityId id, const nlohmann::json& json) {
            auto& transform = world.addComponent<Transform2D>(id);
            
            if (json.contains("position")) {
                auto pos = json["position"];
                if (pos.is_array() && pos.size() >= 2) {
                    transform.position = {pos[0].get<float>(), pos[1].get<float>()};
                }
            }
            
            if (json.contains("rotation")) {
                float degrees = json["rotation"].get<float>();
                transform.rotation = glm::radians(degrees);
            }
            
            if (json.contains("scale")) {
                auto scl = json["scale"];
                if (scl.is_array() && scl.size() >= 2) {
                    transform.scale = {scl[0].get<float>(), scl[1].get<float>()};
                } else if (scl.is_number()) {
                    float uniform = scl.get<float>();
                    transform.scale = {uniform, uniform};
                }
            }
            
            transform.dirty = true;
        },
        // Serializer (NEW)
        [](World& world, EntityId id) -> nlohmann::json {
            auto* transform = world.getComponent<Transform2D>(id);
            if (!transform) {
                return nlohmann::json{};  // Component doesn't exist
            }
            
            nlohmann::json json;
            json["position"] = {transform->position.x, transform->position.y};
            json["rotation"] = glm::degrees(transform->rotation);  // Convert to degrees
            json["scale"] = {transform->scale.x, transform->scale.y};
            
            return json;
        }
    );
}
```

Update ScriptComponent registration:
```cpp
void Application::registerScriptModule() {
    _ecsWorld.registerComponent<ScriptComponent>(
        // Deserializer (existing)
        [&](World& world, EntityId id, const nlohmann::json& json) {
            // ... existing deserializer code ...
        },
        // Serializer (NEW)
        [&](World& world, EntityId id) -> nlohmann::json {
            auto* script = world.getComponent<ScriptComponent>(id);
            if (!script) {
                return nlohmann::json{};
            }
            
            // Get script path from handle (if possible)
            // This might require looking up the script handle
            // For now, return basic info
            nlohmann::json json;
            json["script"] = "unknown";  // TODO: Get actual script path
            // Note: ScriptComponent might not be easily serializable
            // Consider if scripts should be saved or just referenced
            
            return json;
        }
    );
    
    // ... rest of registration ...
}
```

**Action Items:**
- [ ] Add serializer to Transform2D registration
- [ ] Add serializer to ScriptComponent registration (or mark as non-serializable)
- [ ] Add serializers to other registered components
- [ ] Test component serialization works

#### Step 5.1.5: Test Component Serialization

**Create test file:** `tests/core/ecs/ComponentSerialization_test.cpp`

Test cases:
1. Component with serializer can be serialized
2. Serialized component can be deserialized
3. Round-trip: serialize → deserialize → verify identical
4. Component without serializer returns null JSON
5. Non-existent component returns null JSON

**Action Items:**
- [ ] Write component serialization tests
- [ ] Run tests and verify all pass
- [ ] Test with multiple component types

---

## Step 5.2: SceneSerializer

### Objective
Create a SceneSerializer class that can serialize entire scenes (with hierarchy) to JSON and deserialize them back, preserving all entity relationships and component data.

### Design Concept

**Serialization Flow:**
1. Serialize scene metadata (name, version, settings)
2. For each entity:
   - Serialize entity ID (if has one)
   - Serialize entity name (tag)
   - Serialize parent relationship
   - Serialize all components (using registered serializers)
   - Recursively serialize children

**Deserialization Flow:**
1. Parse scene metadata
2. First pass: Create all entities, build ID → EntityId map
3. Second pass: Set parent relationships using ID map
4. Components are deserialized in first pass (already handled)

### Detailed Implementation Steps

#### Step 5.2.1: Create SceneSerializer Header

**File:** `engine/core/scene/SceneSerializer.hpp`

```cpp
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
     */
    void deserializeScene(Scene& scene, const nlohmann::json& data);
    
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
     * @param parent - Parent entity (or invalid for root)
     * @param scene - Scene to add entity to
     * @returns EntityId of created entity
     */
    EntityId deserializeEntity(const nlohmann::json& data, Entity& scene, EntityId parent = EntityId{0, 0});
    
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
     * Builds the entity ID map from JSON entities.
     * Called during deserialization first pass.
     */
    void buildEntityIdMap(const nlohmann::json& entities);
    
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
     */
    std::string getEntityName(EntityId entity) const;
    
    /**
     * Gets all children of an entity.
     */
    std::vector<EntityId> getEntityChildren(EntityId entity) const;
};
```

**Action Items:**
- [ ] Create `engine/core/scene/SceneSerializer.hpp`
- [ ] Declare SceneSerializer class
- [ ] Add all method declarations
- [ ] Verify compilation

#### Step 5.2.2: Implement SceneSerializer

**File:** `engine/core/scene/SceneSerializer.cpp`

1. **Include headers:**
   ```cpp
   #include "SceneSerializer.hpp"
   #include "../scene/SceneGraph.hpp"
   #include "../ecs/component/ComponentRegistry.hpp"
   #include <algorithm>
   ```

2. **Implement constructor:**
   ```cpp
   SceneSerializer::SceneSerializer(World& world, AssetManager& assets)
       : _world(world), _assets(assets) {
   }
   ```

3. **Implement serializeScene():**
   ```cpp
   nlohmann::json SceneSerializer::serializeScene(const Scene& scene) {
       nlohmann::json sceneJson;
       
       // Serialize scene metadata
       sceneJson["name"] = scene.getName();
       sceneJson["version"] = "1.0";  // Could be from scene or config
       
       // Serialize settings (placeholder - can be enhanced)
       nlohmann::json settings;
       // settings["backgroundColor"] = ...;
       // settings["gravity"] = ...;
       sceneJson["settings"] = settings;
       
       // Serialize entities
       nlohmann::json entitiesJson = nlohmann::json::array();
       
       // Serialize root entities (children of scene root)
       scene.forEachRootEntity([&](EntityId entity) {
           nlohmann::json entityJson = serializeEntity(entity, true);
           entitiesJson.push_back(entityJson);
       });
       
       sceneJson["entities"] = entitiesJson;
       
       return sceneJson;
   }
   ```

4. **Implement serializeEntity():**
   ```cpp
   nlohmann::json SceneSerializer::serializeEntity(EntityId entity, bool includeChildren) {
       if (!_world.isAlive(entity)) {
           return nlohmann::json{};
       }
       
       nlohmann::json entityJson;
       
       // Serialize entity ID (if it has a name tag, use that as ID)
       std::string entityName = getEntityName(entity);
       if (!entityName.empty()) {
           entityJson["id"] = entityName;
           entityJson["name"] = entityName;
       }
       
       // Serialize parent (if not root)
       EntityId parent = SceneGraph::getParent(_world, entity);
       if (_world.isAlive(parent)) {
           std::string parentName = getEntityName(parent);
           if (!parentName.empty()) {
               entityJson["parent"] = parentName;
           } else {
               // Parent has no name, can't reference it
               // Could use a different reference system
           }
       } else {
           entityJson["parent"] = nullptr;
       }
       
       // Serialize children IDs (if including children)
       if (includeChildren) {
           auto children = getEntityChildren(entity);
           if (!children.empty()) {
               nlohmann::json childrenArray = nlohmann::json::array();
               for (EntityId child : children) {
                   std::string childName = getEntityName(child);
                   if (!childName.empty()) {
                       childrenArray.push_back(childName);
                   }
               }
               if (!childrenArray.empty()) {
                   entityJson["children"] = childrenArray;
               }
           }
       }
       
       // Serialize components
       nlohmann::json componentsJson;
       
       _world.getRegistry().forEachRegisteredComponent([&](ComponentId componentId) {
           const ComponentInfo* info = _world.getRegistry().getInfo(componentId);
           if (!info || !info->serializeFn) {
               return;  // Component has no serializer
           }
           
           nlohmann::json componentJson = info->serializeFn(_world, entity);
           if (!componentJson.is_null() && !componentJson.empty()) {
               componentsJson[std::string(info->name)] = componentJson;
           }
       });
       
       if (!componentsJson.empty()) {
           entityJson["components"] = componentsJson;
       }
       
       // Serialize children entities (if including)
       if (includeChildren) {
           auto children = getEntityChildren(entity);
           if (!children.empty()) {
               nlohmann::json childrenEntities = nlohmann::json::array();
               for (EntityId child : children) {
                   nlohmann::json childJson = serializeEntity(child, true);
                   if (!childJson.empty()) {
                       childrenEntities.push_back(childJson);
                   }
               }
               if (!childrenEntities.empty()) {
                   // Note: Children are already in main entities array
                   // This is optional - can include here or rely on parent references
               }
           }
       }
       
       return entityJson;
   }
   ```

5. **Implement serializeComponent():**
   ```cpp
   nlohmann::json SceneSerializer::serializeComponent(EntityId entity, ComponentId componentId) {
       const ComponentInfo* info = _world.getRegistry().getInfo(componentId);
       if (!info || !info->serializeFn) {
           return nlohmann::json{};  // No serializer
       }
       
       return info->serializeFn(_world, entity);
   }
   ```

6. **Implement deserializeScene():**
   ```cpp
   void SceneSerializer::deserializeScene(Scene& scene, const nlohmann::json& data) {
       // Clear ID map
       _idMap.clear();
       _nextTempId = 1;
       
       // Parse scene metadata
       if (data.contains("name")) {
           // Scene name is set in constructor, but could update here
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
   ```

7. **Implement deserializeEntity():**
   ```cpp
   EntityId SceneSerializer::deserializeEntity(const nlohmann::json& data, Scene& scene, EntityId parent) {
       // Create entity in scene
       EntityId entity = parent == scene.getRoot() 
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
   ```

8. **Implement deserializeComponent():**
   ```cpp
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
   ```

9. **Implement helper methods:**
   ```cpp
   void SceneSerializer::buildEntityIdMap(const nlohmann::json& entities) {
       _idMap.clear();
       
       for (const auto& entityJson : entities) {
           if (!entityJson.contains("id")) {
               continue;
           }
           
           std::string id = entityJson["id"].get<std::string>();
           // EntityId will be set during deserialization
           // This is a placeholder - actual mapping happens in deserializeEntity
       }
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
       // Get name from tags (entities are named via tags)
       // This is a simplified version - might need to check all tags
       // For now, return first tag or empty
       
       // TODO: Implement proper name retrieval from tags
       // Could iterate through tags and find one that looks like a name
       return "";  // Placeholder
   }
   
   std::vector<EntityId> SceneSerializer::getEntityChildren(EntityId entity) const {
       return SceneGraph::getChildren(_world, entity);
   }
   ```

**Note:** `getEntityName()` needs proper implementation. Entities are named via tags, so we need a way to get the "name" tag.

**Action Items:**
- [ ] Create `engine/core/scene/SceneSerializer.cpp`
- [ ] Implement all methods
- [ ] Implement getEntityName() properly (get name tag)
- [ ] Test serialization round-trip
- [ ] Fix any issues

#### Step 5.2.3: Implement Entity Name Retrieval

**Problem:** Need to get entity name from tags for serialization.

**Solution:** Add helper to get name tag, or use a specific naming convention.

**File:** `engine/core/scene/SceneSerializer.cpp`

Implement getEntityName():
```cpp
std::string SceneSerializer::getEntityName(EntityId entity) const {
    // Option 1: Use a specific tag convention (e.g., "name" tag)
    // Option 2: Get first tag (if entities only have one tag)
    // Option 3: Use a NameComponent (if exists)
    
    // For now, we'll check if entity has a tag that looks like a name
    // This is a simplified approach - might need enhancement
    
    // TODO: Implement proper name retrieval
    // Could add World::getEntityName() or similar
    // Or use a convention where the "name" tag is the entity name
    
    // Placeholder: return empty (entities without names won't have IDs in JSON)
    return "";
}
```

**Alternative:** Add a helper to World or use tag system properly. For now, we can work with entities that have name tags.

**Action Items:**
- [ ] Implement getEntityName() (may need World helper)
- [ ] Test name retrieval works
- [ ] Document naming convention

#### Step 5.2.4: Test SceneSerializer

**Create test file:** `tests/core/scene/SceneSerializer_test.cpp`

Test cases:
1. serializeScene() creates valid JSON
2. deserializeScene() loads scene correctly
3. Round-trip: serialize → deserialize → verify identical
4. Entity hierarchy preserved
5. Component data preserved
6. Entity references work correctly
7. Entities without IDs handled correctly

**Action Items:**
- [ ] Write SceneSerializer tests
- [ ] Run tests and verify all pass
- [ ] Test with complex hierarchies
- [ ] Test edge cases

---

## Step 5.3: Enhanced Scene JSON Format

### Objective
Enhance the scene JSON format to support scene settings, version information, and better organization.

### Current Format
```json
{
  "name": "Level 1",
  "entities": [...]
}
```

### Enhanced Format
```json
{
  "name": "Level 1",
  "version": "1.0",
  "settings": {
    "backgroundColor": [0.1, 0.1, 0.15, 1.0],
    "gravity": [0, -9.8],
    "bounds": { "min": [-1000, -1000], "max": [1000, 1000] }
  },
  "entities": [...]
}
```

### Detailed Implementation Steps

#### Step 5.3.1: Define Scene Settings Structure

**File:** `engine/core/scene/SceneSettings.hpp` (optional, or inline in Scene)

```cpp
#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

/**
 * Scene settings (background color, physics, bounds, etc.)
 */
struct SceneSettings {
    glm::vec4 backgroundColor{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec2 gravity{0.0f, -9.8f};
    
    struct Bounds {
        glm::vec2 min{-1000.0f, -1000.0f};
        glm::vec2 max{1000.0f, 1000.0f};
    } bounds;
    
    /**
     * Serializes settings to JSON.
     */
    nlohmann::json toJson() const {
        nlohmann::json json;
        json["backgroundColor"] = {
            backgroundColor.r, backgroundColor.g, backgroundColor.b, backgroundColor.a
        };
        json["gravity"] = {gravity.x, gravity.y};
        json["bounds"]["min"] = {bounds.min.x, bounds.min.y};
        json["bounds"]["max"] = {bounds.max.x, bounds.max.y};
        return json;
    }
    
    /**
     * Deserializes settings from JSON.
     */
    void fromJson(const nlohmann::json& json) {
        if (json.contains("backgroundColor")) {
            auto bg = json["backgroundColor"];
            if (bg.is_array() && bg.size() >= 4) {
                backgroundColor = {bg[0], bg[1], bg[2], bg[3]};
            }
        }
        
        if (json.contains("gravity")) {
            auto g = json["gravity"];
            if (g.is_array() && g.size() >= 2) {
                gravity = {g[0], g[1]};
            }
        }
        
        if (json.contains("bounds")) {
            auto b = json["bounds"];
            if (b.contains("min")) {
                auto min = b["min"];
                if (min.is_array() && min.size() >= 2) {
                    bounds.min = {min[0], min[1]};
                }
            }
            if (b.contains("max")) {
                auto max = b["max"];
                if (max.is_array() && max.size() >= 2) {
                    bounds.max = {max[0], max[1]};
                }
            }
        }
    }
};
```

**Action Items:**
- [ ] Create SceneSettings structure (or add to Scene)
- [ ] Implement serialization/deserialization
- [ ] Test settings load/save correctly

#### Step 5.3.2: Add Settings to Scene Class

**File:** `engine/core/scene/Scene.hpp`

Add to Scene class:
```cpp
class Scene {
    // ... existing members ...
    
private:
    // ... existing private members ...
    
    SceneSettings _settings;  // Scene settings
    
public:
    /**
     * Gets scene settings.
     */
    const SceneSettings& getSettings() const {
        return _settings;
    }
    
    /**
     * Gets mutable scene settings.
     */
    SceneSettings& getSettings() {
        return _settings;
    }
    
    // ... rest of class ...
};
```

**Action Items:**
- [ ] Add settings to Scene class
- [ ] Add getters for settings
- [ ] Update Scene constructor if needed

#### Step 5.3.3: Update SceneSerializer for Settings

**File:** `engine/core/scene/SceneSerializer.cpp`

Update serializeScene():
```cpp
nlohmann::json SceneSerializer::serializeScene(const Scene& scene) {
    nlohmann::json sceneJson;
    
    sceneJson["name"] = scene.getName();
    sceneJson["version"] = "1.0";
    
    // Serialize settings
    sceneJson["settings"] = scene.getSettings().toJson();
    
    // ... rest of serialization ...
}
```

Update deserializeScene():
```cpp
void SceneSerializer::deserializeScene(Scene& scene, const nlohmann::json& data) {
    // Deserialize settings
    if (data.contains("settings")) {
        scene.getSettings().fromJson(data["settings"]);
    }
    
    // ... rest of deserialization ...
}
```

**Action Items:**
- [ ] Update SceneSerializer to handle settings
- [ ] Test settings serialization
- [ ] Test settings deserialization

---

## Step 5.4: SaveData Structure

### Objective
Define the structure for save game files, including metadata and game state.

### Detailed Implementation Steps

#### Step 5.4.1: Create SaveData Header

**File:** `engine/core/save/SaveData.hpp`

```cpp
#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

/**
 * Metadata about a save file.
 */
struct SaveMetadata {
    std::string saveName;          // User-friendly name (e.g., "Save Slot 1")
    std::string timestamp;         // ISO 8601 timestamp
    std::string gameVersion;       // Game version when saved
    std::string screenshotPath;    // Optional thumbnail image path
    uint32_t playTimeSeconds;      // Total play time
    std::string currentScene;      // Scene that was active when saved
    
    /**
     * Serializes metadata to JSON.
     */
    nlohmann::json toJson() const {
        nlohmann::json json;
        json["saveName"] = saveName;
        json["timestamp"] = timestamp;
        json["gameVersion"] = gameVersion;
        if (!screenshotPath.empty()) {
            json["screenshotPath"] = screenshotPath;
        }
        json["playTimeSeconds"] = playTimeSeconds;
        json["currentScene"] = currentScene;
        return json;
    }
    
    /**
     * Deserializes metadata from JSON.
     */
    void fromJson(const nlohmann::json& json) {
        if (json.contains("saveName")) {
            saveName = json["saveName"].get<std::string>();
        }
        if (json.contains("timestamp")) {
            timestamp = json["timestamp"].get<std::string>();
        }
        if (json.contains("gameVersion")) {
            gameVersion = json["gameVersion"].get<std::string>();
        }
        if (json.contains("screenshotPath")) {
            screenshotPath = json["screenshotPath"].get<std::string>();
        }
        if (json.contains("playTimeSeconds")) {
            playTimeSeconds = json["playTimeSeconds"].get<uint32_t>();
        }
        if (json.contains("currentScene")) {
            currentScene = json["currentScene"].get<std::string>();
        }
    }
};

/**
 * Complete save game data.
 */
struct SaveData {
    SaveMetadata metadata;
    nlohmann::json sceneState;       // Current scene serialized
    nlohmann::json persistentState;  // Cross-scene data (inventory, unlocks, etc.)
    nlohmann::json gameSettings;     // Player preferences
    
    /**
     * Serializes save data to JSON.
     */
    nlohmann::json toJson() const {
        nlohmann::json json;
        json["metadata"] = metadata.toJson();
        json["sceneState"] = sceneState;
        json["persistentState"] = persistentState;
        json["gameSettings"] = gameSettings;
        return json;
    }
    
    /**
     * Deserializes save data from JSON.
     */
    void fromJson(const nlohmann::json& json) {
        if (json.contains("metadata")) {
            metadata.fromJson(json["metadata"]);
        }
        if (json.contains("sceneState")) {
            sceneState = json["sceneState"];
        }
        if (json.contains("persistentState")) {
            persistentState = json["persistentState"];
        }
        if (json.contains("gameSettings")) {
            gameSettings = json["gameSettings"];
        }
    }
};
```

**Action Items:**
- [ ] Create `engine/core/save/SaveData.hpp`
- [ ] Define SaveMetadata and SaveData structs
- [ ] Implement serialization/deserialization
- [ ] Test SaveData serialization

---

## Step 5.5: SaveManager

### Objective
Create a complete save/load system with save slots, persistent state, and auto-save functionality.

### Detailed Implementation Steps

#### Step 5.5.1: Create SaveManager Header

**File:** `engine/core/save/SaveManager.hpp`

```cpp
#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "../scene/SceneManager.hpp"
#include "../ecs/World.hpp"
#include "SaveData.hpp"

/**
 * SaveManager handles saving and loading game state.
 * Supports multiple save slots, persistent state, and auto-save.
 */
class SaveManager {
public:
    /**
     * Creates a SaveManager.
     * @param scenes - Reference to SceneManager
     * @param world - Reference to ECS World
     */
    SaveManager(SceneManager& scenes, World& world);
    
    // ====================================================================
    // Save Operations
    // ====================================================================
    
    /**
     * Saves the game to a named slot.
     * @param slotName - Name of the save slot
     * @returns true on success, false on failure
     */
    bool save(const std::string& slotName);
    
    /**
     * Saves the game to a specific file path.
     * @param path - Path to save file
     * @returns true on success, false on failure
     */
    bool saveToFile(const std::filesystem::path& path);
    
    /**
     * Performs a quick save (to a special quick-save slot).
     * @returns true on success
     */
    bool quickSave();
    
    /**
     * Performs an auto-save (if enabled).
     * @returns true on success
     */
    bool autoSave();
    
    // ====================================================================
    // Load Operations
    // ====================================================================
    
    /**
     * Loads a game from a named slot.
     * @param slotName - Name of the save slot
     * @returns true on success, false on failure
     */
    bool load(const std::string& slotName);
    
    /**
     * Loads a game from a specific file path.
     * @param path - Path to save file
     * @returns true on success, false on failure
     */
    bool loadFromFile(const std::filesystem::path& path);
    
    /**
     * Loads the quick-save slot.
     * @returns true on success
     */
    bool quickLoad();
    
    // ====================================================================
    // Save Slot Management
    // ====================================================================
    
    /**
     * Lists all available save slots.
     * @returns Vector of save metadata
     */
    std::vector<SaveMetadata> listSaves();
    
    /**
     * Deletes a save slot.
     * @param slotName - Name of the save slot to delete
     * @returns true on success
     */
    bool deleteSave(const std::string& slotName);
    
    /**
     * Renames a save slot.
     * @param oldName - Current name
     * @param newName - New name
     * @returns true on success
     */
    bool renameSave(const std::string& oldName, const std::string& newName);
    
    /**
     * Copies a save slot.
     * @param source - Source slot name
     * @param dest - Destination slot name
     * @returns true on success
     */
    bool copySave(const std::string& source, const std::string& dest);
    
    // ====================================================================
    // Persistent State
    // ====================================================================
    
    /**
     * Sets a persistent state value (survives scene transitions).
     * @param key - Key name
     * @param value - JSON value
     */
    void setPersistent(const std::string& key, const nlohmann::json& value);
    
    /**
     * Gets a persistent state value.
     * @param key - Key name
     * @returns JSON value, or null if not found
     */
    nlohmann::json getPersistent(const std::string& key) const;
    
    /**
     * Checks if a persistent state key exists.
     * @param key - Key name
     * @returns true if key exists
     */
    bool hasPersistent(const std::string& key) const;
    
    /**
     * Clears a persistent state key.
     * @param key - Key name
     */
    void clearPersistent(const std::string& key);
    
    /**
     * Clears all persistent state.
     */
    void clearAllPersistent();
    
    // ====================================================================
    // Configuration
    // ====================================================================
    
    /**
     * Sets the save directory.
     * @param dir - Directory path for save files
     */
    void setSaveDirectory(const std::filesystem::path& dir);
    
    /**
     * Gets the save directory.
     */
    const std::filesystem::path& getSaveDirectory() const {
        return _saveDirectory;
    }
    
    /**
     * Sets the auto-save interval.
     * @param seconds - Interval in seconds
     */
    void setAutoSaveInterval(float seconds);
    
    /**
     * Gets the auto-save interval.
     */
    float getAutoSaveInterval() const {
        return _autoSaveInterval;
    }
    
    /**
     * Enables or disables auto-save.
     * @param enable - True to enable
     */
    void enableAutoSave(bool enable);
    
    /**
     * Checks if auto-save is enabled.
     */
    bool isAutoSaveEnabled() const {
        return _autoSaveEnabled;
    }
    
    /**
     * Updates the save manager (call every frame).
     * Handles auto-save timer.
     * @param dt - Delta time in seconds
     */
    void update(float dt);
    
private:
    SceneManager& _scenes;
    World& _world;
    
    std::filesystem::path _saveDirectory{"saves"};
    nlohmann::json _persistentState;
    nlohmann::json _gameSettings;
    
    float _autoSaveInterval{300.0f};  // 5 minutes default
    float _autoSaveTimer{0.0f};
    bool _autoSaveEnabled{false};
    
    /**
     * Builds save data from current game state.
     */
    SaveData buildSaveData();
    
    /**
     * Applies save data to restore game state.
     */
    void applySaveData(const SaveData& data);
    
    /**
     * Gets the file path for a save slot.
     * @param slotName - Save slot name
     * @returns Full path to save file
     */
    std::filesystem::path getSavePath(const std::string& slotName) const;
    
    /**
     * Generates a timestamp string.
     */
    std::string generateTimestamp() const;
};
```

**Action Items:**
- [ ] Create `engine/core/save/SaveManager.hpp`
- [ ] Declare SaveManager class
- [ ] Add all method declarations
- [ ] Verify compilation

#### Step 5.5.2: Implement SaveManager

**File:** `engine/core/save/SaveManager.cpp`

1. **Include headers:**
   ```cpp
   #include "SaveManager.hpp"
   #include "../scene/SceneSerializer.hpp"
   #include "../app/GameManifest.hpp"
   #include <fstream>
   #include <iomanip>
   #include <sstream>
   #include <ctime>
   ```

2. **Implement constructor:**
   ```cpp
   SaveManager::SaveManager(SceneManager& scenes, World& world)
       : _scenes(scenes), _world(world) {
       // Create save directory if it doesn't exist
       if (!std::filesystem::exists(_saveDirectory)) {
           std::filesystem::create_directories(_saveDirectory);
       }
   }
   ```

3. **Implement buildSaveData():**
   ```cpp
   SaveData SaveManager::buildSaveData() {
       SaveData saveData;
       
       // Build metadata
       saveData.metadata.timestamp = generateTimestamp();
       saveData.metadata.gameVersion = "1.0.0";  // TODO: Get from GameManifest
       saveData.metadata.playTimeSeconds = 0;    // TODO: Track play time
       
       // Get current scene
       Scene* activeScene = _scenes.getActiveScene();
       if (activeScene) {
           saveData.metadata.currentScene = activeScene->getName();
       }
       
       // Serialize current scene
       if (activeScene) {
           SceneSerializer serializer(_world, /* AssetManager */);
           saveData.sceneState = serializer.serializeScene(*activeScene);
       }
       
       // Copy persistent state
       saveData.persistentState = _persistentState;
       
       // Copy game settings
       saveData.gameSettings = _gameSettings;
       
       return saveData;
   }
   ```

4. **Implement applySaveData():**
   ```cpp
   void SaveManager::applySaveData(const SaveData& data) {
       // Restore persistent state
       _persistentState = data.persistentState;
       
       // Restore game settings
       _gameSettings = data.gameSettings;
       
       // Load scene
       if (!data.metadata.currentScene.empty()) {
           // Unload current scenes
           _scenes.unloadAllScenes();
           
           // Load saved scene
           SceneHandle handle = _scenes.loadScene(data.metadata.currentScene);
           Scene* scene = _scenes.getScene(handle);
           
           if (scene && !data.sceneState.empty()) {
               // Deserialize scene state
               SceneSerializer serializer(_world, /* AssetManager */);
               serializer.deserializeScene(*scene, data.sceneState);
           }
       }
   }
   ```

5. **Implement save operations:**
   ```cpp
   bool SaveManager::save(const std::string& slotName) {
       if (slotName.empty()) {
           return false;
       }
       
       SaveData saveData = buildSaveData();
       saveData.metadata.saveName = slotName;
       
       std::filesystem::path savePath = getSavePath(slotName);
       return saveToFile(savePath, saveData);
   }
   
   bool SaveManager::saveToFile(const std::filesystem::path& path) {
       SaveData saveData = buildSaveData();
       if (saveData.metadata.saveName.empty()) {
           saveData.metadata.saveName = path.stem().string();
       }
       return saveToFile(path, saveData);
   }
   
   bool SaveManager::saveToFile(const std::filesystem::path& path, const SaveData& data) {
       try {
           // Ensure directory exists
           std::filesystem::create_directories(path.parent_path());
           
           // Serialize to JSON
           nlohmann::json json = data.toJson();
           
           // Write to file
           std::ofstream file(path);
           if (!file.is_open()) {
               return false;
           }
           
           file << json.dump(2);  // Pretty print with 2-space indent
           file.close();
           
           return true;
       } catch (const std::exception&) {
           return false;
       }
   }
   
   bool SaveManager::quickSave() {
       return save("quicksave");
   }
   
   bool SaveManager::autoSave() {
       return save("autosave");
   }
   ```

6. **Implement load operations:**
   ```cpp
   bool SaveManager::load(const std::string& slotName) {
       if (slotName.empty()) {
           return false;
       }
       
       std::filesystem::path savePath = getSavePath(slotName);
       return loadFromFile(savePath);
   }
   
   bool SaveManager::loadFromFile(const std::filesystem::path& path) {
       try {
           if (!std::filesystem::exists(path)) {
               return false;  // File doesn't exist
           }
           
           // Read file
           std::ifstream file(path);
           if (!file.is_open()) {
               return false;
           }
           
           nlohmann::json json;
           file >> json;
           file.close();
           
           // Deserialize
           SaveData saveData;
           saveData.fromJson(json);
           
           // Apply save data
           applySaveData(saveData);
           
           return true;
       } catch (const std::exception&) {
           return false;
       }
   }
   
   bool SaveManager::quickLoad() {
       return load("quicksave");
   }
   ```

7. **Implement save slot management:**
   ```cpp
   std::vector<SaveMetadata> SaveManager::listSaves() {
       std::vector<SaveMetadata> saves;
       
       if (!std::filesystem::exists(_saveDirectory)) {
           return saves;
       }
       
       // Iterate through save directory
       for (const auto& entry : std::filesystem::directory_iterator(_saveDirectory)) {
           if (entry.is_regular_file() && entry.path().extension() == ".save") {
               try {
                   std::ifstream file(entry.path());
                   if (!file.is_open()) continue;
                   
                   nlohmann::json json;
                   file >> json;
                   file.close();
                   
                   SaveMetadata metadata;
                   if (json.contains("metadata")) {
                       metadata.fromJson(json["metadata"]);
                       saves.push_back(metadata);
                   }
               } catch (const std::exception&) {
                   // Skip invalid save files
                   continue;
               }
           }
       }
       
       return saves;
   }
   
   bool SaveManager::deleteSave(const std::string& slotName) {
       std::filesystem::path savePath = getSavePath(slotName);
       if (std::filesystem::exists(savePath)) {
           return std::filesystem::remove(savePath);
       }
       return false;
   }
   
   bool SaveManager::renameSave(const std::string& oldName, const std::string& newName) {
       std::filesystem::path oldPath = getSavePath(oldName);
       std::filesystem::path newPath = getSavePath(newName);
       
       if (!std::filesystem::exists(oldPath)) {
           return false;
       }
       
       try {
           std::filesystem::rename(oldPath, newPath);
           
           // Update metadata in file
           std::ifstream file(newPath);
           nlohmann::json json;
           file >> json;
           file.close();
           
           json["metadata"]["saveName"] = newName;
           
           std::ofstream outFile(newPath);
           outFile << json.dump(2);
           outFile.close();
           
           return true;
       } catch (const std::exception&) {
           return false;
       }
   }
   
   bool SaveManager::copySave(const std::string& source, const std::string& dest) {
       std::filesystem::path sourcePath = getSavePath(source);
       std::filesystem::path destPath = getSavePath(dest);
       
       if (!std::filesystem::exists(sourcePath)) {
           return false;
       }
       
       try {
           std::filesystem::copy_file(sourcePath, destPath);
           
           // Update metadata in copied file
           std::ifstream file(destPath);
           nlohmann::json json;
           file >> json;
           file.close();
           
           json["metadata"]["saveName"] = dest;
           
           std::ofstream outFile(destPath);
           outFile << json.dump(2);
           outFile.close();
           
           return true;
       } catch (const std::exception&) {
           return false;
       }
   }
   ```

8. **Implement persistent state:**
   ```cpp
   void SaveManager::setPersistent(const std::string& key, const nlohmann::json& value) {
       _persistentState[key] = value;
   }
   
   nlohmann::json SaveManager::getPersistent(const std::string& key) const {
       if (_persistentState.contains(key)) {
           return _persistentState[key];
       }
       return nlohmann::json{};
   }
   
   bool SaveManager::hasPersistent(const std::string& key) const {
       return _persistentState.contains(key);
   }
   
   void SaveManager::clearPersistent(const std::string& key) {
       _persistentState.erase(key);
   }
   
   void SaveManager::clearAllPersistent() {
       _persistentState.clear();
   }
   ```

9. **Implement configuration:**
   ```cpp
   void SaveManager::setSaveDirectory(const std::filesystem::path& dir) {
       _saveDirectory = dir;
       if (!std::filesystem::exists(_saveDirectory)) {
           std::filesystem::create_directories(_saveDirectory);
       }
   }
   
   void SaveManager::setAutoSaveInterval(float seconds) {
       _autoSaveInterval = seconds > 0.0f ? seconds : 300.0f;
   }
   
   void SaveManager::enableAutoSave(bool enable) {
       _autoSaveEnabled = enable;
       if (enable) {
           _autoSaveTimer = 0.0f;  // Reset timer
       }
   }
   ```

10. **Implement update():**
    ```cpp
    void SaveManager::update(float dt) {
        if (!_autoSaveEnabled) {
            return;
        }
        
        _autoSaveTimer += dt;
        if (_autoSaveTimer >= _autoSaveInterval) {
            autoSave();
            _autoSaveTimer = 0.0f;
        }
    }
    ```

11. **Implement helper methods:**
    ```cpp
    std::filesystem::path SaveManager::getSavePath(const std::string& slotName) const {
        // Sanitize slot name (remove invalid characters)
        std::string sanitized = slotName;
        std::replace(sanitized.begin(), sanitized.end(), ' ', '_');
        std::replace(sanitized.begin(), sanitized.end(), '/', '_');
        std::replace(sanitized.begin(), sanitized.end(), '\\', '_');
        
        return _saveDirectory / (sanitized + ".save");
    }
    
    std::string SaveManager::generateTimestamp() const {
        auto now = std::time(nullptr);
        auto tm = *std::localtime(&now);
        
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
        return oss.str();
    }
    ```

**Note:** SaveManager needs access to AssetManager for SceneSerializer. Add it to constructor.

**Action Items:**
- [ ] Create `engine/core/save/SaveManager.cpp`
- [ ] Implement all methods
- [ ] Add AssetManager to constructor
- [ ] Test save/load operations
- [ ] Test persistent state
- [ ] Test auto-save

#### Step 5.5.3: Integrate SaveManager into Application

**File:** `engine/core/app/Application.hpp`

Add SaveManager:
```cpp
class Application {
    // ... existing members ...
    
    SaveManager& getSaveManager() {
        return _saveManager;
    }
    
private:
    // ... existing members ...
    
    SaveManager _saveManager;
    
    // ... rest of class ...
};
```

**File:** `engine/core/app/Application.cpp`

Update constructor:
```cpp
Application::Application(const std::filesystem::path& rootDir, const std::filesystem::path& manifestPath)
    : _manifestPath(manifestPath), 
      _rootDir(rootDir), 
      _assetManager(_rootDir), 
      _sceneManager(_ecsWorld, _assetManager, _eventBus),
      _saveManager(_sceneManager, _ecsWorld) {
}
```

Update run() to call SaveManager::update():
```cpp
void Application::run() {
    // ... existing code ...
    
    while (_running) {
        // ... existing update code ...
        
        _sceneManager.update(dt);
        _saveManager.update(dt);  // NEW
        
        // ... rest of loop ...
    }
}
```

**Action Items:**
- [ ] Add SaveManager to Application
- [ ] Update constructor
- [ ] Add update() call in run()
- [ ] Test integration

---

## Step 5.6: Saveable Component Marker

### Objective
Create a system to mark which components should be saved and which should be excluded from saves.

### Design

**Saveable Components:**
- PlayerStats, Inventory, GameState - Should be saved
- ParticleEmitter, TemporaryEffects - Should NOT be saved

**Approach:**
- Add `static constexpr bool saveable` to components
- SceneSerializer checks this flag before serializing
- Default to `true` (most components are saveable)

### Detailed Implementation Steps

#### Step 5.6.1: Define SaveableComponent Concept

**File:** `engine/core/ecs/component/ComponentConcepts.hpp` (or create new file)

Add concept:
```cpp
#pragma once

#include "Component.hpp"

/**
 * Concept for components that can be marked as saveable.
 */
template<typename T>
concept SaveableComponent = ComponentType<T> && requires {
    { T::saveable } -> std::convertible_to<bool>;
};

/**
 * Helper to check if a component type is saveable.
 */
template<ComponentType T>
constexpr bool isComponentSaveable() {
    if constexpr (requires { T::saveable; }) {
        return T::saveable;
    }
    return true;  // Default: saveable
}
```

**Action Items:**
- [ ] Add SaveableComponent concept
- [ ] Add isComponentSaveable() helper
- [ ] Verify compilation

#### Step 5.6.2: Update Component Registration

**File:** `engine/core/ecs/component/ComponentInfo.hpp`

Add saveable flag (optional):
```cpp
struct ComponentInfo {
    std::string_view name;
    size_t size;
    ComponentId id;
    std::function<void(World&, EntityId, const nlohmann::json&)> deserializeFn;
    std::function<nlohmann::json(World&, EntityId)> serializeFn;
    bool saveable{true};  // Default: saveable
    
    explicit operator bool() const {
        return !name.empty();
    }
};
```

**Action Items:**
- [ ] Add saveable flag to ComponentInfo
- [ ] Update ComponentRegistry to set saveable flag
- [ ] Verify compilation

#### Step 5.6.3: Update SceneSerializer to Check Saveable Flag

**File:** `engine/core/scene/SceneSerializer.cpp`

Update serializeEntity() to check saveable flag:
```cpp
nlohmann::json SceneSerializer::serializeEntity(EntityId entity, bool includeChildren) {
    // ... existing code ...
    
    // Serialize components
    nlohmann::json componentsJson;
    
    _world.getRegistry().forEachRegisteredComponent([&](ComponentId componentId) {
        const ComponentInfo* info = _world.getRegistry().getInfo(componentId);
        if (!info || !info->serializeFn) {
            return;
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
    
    // ... rest of method ...
}
```

**Action Items:**
- [ ] Update SceneSerializer to check saveable flag
- [ ] Test that non-saveable components are excluded
- [ ] Test that saveable components are included

#### Step 5.6.4: Mark Components as Saveable/Non-Saveable

**Example components:**

**Saveable component:**
```cpp
struct PlayerStats : Component<PlayerStats> {
    COMPONENT_NAME("PlayerStats");
    static constexpr bool saveable = true;  // Should be saved
    
    int health{100};
    int maxHealth{100};
    int gold{0};
};
```

**Non-saveable component:**
```cpp
struct ParticleEmitter : Component<ParticleEmitter> {
    COMPONENT_NAME("ParticleEmitter");
    static constexpr bool saveable = false;  // Don't save particle state
    
    // ... particle emitter data ...
};
```

**Action Items:**
- [ ] Mark appropriate components as saveable
- [ ] Mark temporary/effect components as non-saveable
- [ ] Test save filtering works

---

## Step 5.7: Integration & Comprehensive Testing

### Objective
Ensure all Phase 5 components work together correctly and save/load system is robust.

### Integration Points

#### Step 5.7.1: Verify Serialization Round-Trip

**Test that serialization preserves all data:**
- [ ] Serialize scene → deserialize → verify identical
- [ ] Entity hierarchy preserved
- [ ] Component data preserved
- [ ] Entity references work
- [ ] Settings preserved

#### Step 5.7.2: Verify Save/Load System

**Test save/load functionality:**
- [ ] Save game → load game → verify state restored
- [ ] Multiple save slots work
- [ ] Quick save/load works
- [ ] Auto-save works
- [ ] Persistent state survives scene transitions

#### Step 5.7.3: End-to-End Integration Test

**Create comprehensive test:**
```cpp
TEST(SaveManager, EndToEnd_SaveLoad) {
    // Setup
    World world;
    AssetManager assets("test_assets");
    EventBus events;
    SceneManager scenes(world, assets, events);
    SaveManager saves(scenes, world);
    
    // Load initial scene
    scenes.loadScene("test_scene.json");
    
    // Modify game state
    // ... modify entities, components, etc. ...
    
    // Save game
    bool saved = saves.save("test_save");
    EXPECT_TRUE(saved);
    
    // Modify state again
    // ... more modifications ...
    
    // Load game
    bool loaded = saves.load("test_save");
    EXPECT_TRUE(loaded);
    
    // Verify state restored
    // ... verify entities, components match saved state ...
}
```

**Action Items:**
- [ ] Write end-to-end save/load test
- [ ] Run test and verify all functionality
- [ ] Fix any integration issues

#### Step 5.7.4: Performance Testing

**Test serialization performance:**
- [ ] Large scene (1000+ entities) serialization time
- [ ] Large save file load time
- [ ] Memory usage during save/load

**Action Items:**
- [ ] Write performance tests
- [ ] Profile serialization
- [ ] Optimize if needed

---

## Phase 5 Completion Checklist

### Functional Requirements
- [ ] Components can be serialized to JSON
- [ ] Components can be deserialized from JSON
- [ ] SceneSerializer serializes entire scenes
- [ ] SceneSerializer deserializes entire scenes
- [ ] Entity hierarchy preserved in serialization
- [ ] SaveManager saves game state
- [ ] SaveManager loads game state
- [ ] Multiple save slots work
- [ ] Persistent state works
- [ ] Auto-save works
- [ ] Saveable component filtering works

### Code Quality
- [ ] Code compiles without warnings
- [ ] All functions documented
- [ ] Error handling implemented
- [ ] File I/O error handling
- [ ] Memory safety verified
- [ ] Code follows project conventions

### Integration
- [ ] SaveManager integrates with Application
- [ ] SceneSerializer integrates with SceneManager
- [ ] Works with Phase 1, 2, 3 components
- [ ] Ready for use in games

### Testing
- [ ] Component serialization tests pass
- [ ] SceneSerializer tests pass
- [ ] SaveManager tests pass
- [ ] Integration tests pass
- [ ] Round-trip tests pass
- [ ] Performance tests pass

---

## Estimated Time Breakdown

- **Step 5.1 (Component Serialization Registration):** 4-5 hours
- **Step 5.2 (SceneSerializer):** 10-12 hours
- **Step 5.3 (Enhanced JSON Format):** 3-4 hours
- **Step 5.4 (SaveData Structure):** 2-3 hours
- **Step 5.5 (SaveManager):** 12-15 hours
- **Step 5.6 (Saveable Marker):** 3-4 hours
- **Step 5.7 (Integration & Testing):** 6-8 hours

**Total Phase 5:** 40-51 hours

---

## Common Pitfalls & Solutions

### Pitfall 1: Entity Reference Resolution
**Issue:** Entity IDs in JSON don't match EntityIds after load
**Solution:** Build ID map in first pass, resolve in second pass

### Pitfall 2: Circular References
**Issue:** Serialization creates circular JSON structures
**Solution:** Use entity IDs for references, not nested objects

### Pitfall 3: Component Serialization Missing
**Issue:** Component has no serializer, can't be saved
**Solution:** Add serializer to registration, or mark as non-saveable

### Pitfall 4: File I/O Errors
**Issue:** Save/load fails silently
**Solution:** Proper error handling, return bool, log errors

### Pitfall 5: Persistent State Not Restored
**Issue:** Persistent state lost on load
**Solution:** Ensure persistent state is included in SaveData and restored

---

## Dependencies on Previous Phases

Phase 5 requires:
- ✅ Phase 1: Transform2D (needs serializer)
- ✅ Phase 2: Scene, SceneManager (for serialization)
- ✅ Phase 3: SceneManager with events (for save/load integration)
- ✅ nlohmann/json library

If previous phases are not complete, Phase 5 cannot proceed.

---

## File Format Specification

### Save File Format
```json
{
  "metadata": {
    "saveName": "Save Slot 1",
    "timestamp": "2024-01-15T14:30:00",
    "gameVersion": "1.0.0",
    "playTimeSeconds": 3600,
    "currentScene": "scenes/level1.scene.json"
  },
  "sceneState": {
    "name": "Level 1",
    "version": "1.0",
    "settings": {...},
    "entities": [...]
  },
  "persistentState": {
    "playerInventory": {...},
    "unlockedLevels": [...],
    "gameProgress": {...}
  },
  "gameSettings": {
    "volume": 0.8,
    "fullscreen": true,
    "keybindings": {...}
  }
}
```

**Action Items:**
- [ ] Document save file format
- [ ] Create example save file
- [ ] Test format compatibility

---

## Next Steps After Phase 5

Once Phase 5 is complete:
1. ✅ Complete save/load system is functional
2. ✅ Scenes can be saved and restored
3. ✅ Persistent state system works
4. ✅ Ready for game development
5. ✅ Ready for Phase 6 (polish and integration)

**Phase 5 provides complete serialization and save/load - ensure it's robust and handles edge cases!**
