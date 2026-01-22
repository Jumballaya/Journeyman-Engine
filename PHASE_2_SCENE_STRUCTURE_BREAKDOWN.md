# Phase 2: Scene Structure - Comprehensive Implementation Breakdown

## Overview

Phase 2 builds upon Phase 1's transform hierarchy foundation to create the Scene management system. This phase implements:
1. **SceneHandle** - A handle type for referencing scenes (similar to EntityId pattern)
2. **Scene Class** - Manages a collection of entities with a root node, lifecycle states, and entity tracking
3. **Additional SceneGraph Utilities** - Path finding and tree traversal functions
4. **Basic SceneManager** - Loads scenes from JSON, manages active scene, migrates from SceneLoader
5. **Enhanced Scene JSON Format** - Support for entity IDs, parent-child relationships in JSON

**Goal:** Enable the engine to manage multiple scenes, load scenes with hierarchical entity structures, and provide a foundation for advanced scene management features in Phase 3.

**Success Criteria:**
- ✅ Scenes can be created and managed independently
- ✅ Each scene has an invisible root entity
- ✅ Entities can be created within scenes with parent-child relationships
- ✅ Scenes can be loaded from JSON with hierarchy support
- ✅ SceneManager can load and track multiple scenes
- ✅ Scene lifecycle states work correctly

**Prerequisites:**
- ✅ Phase 1 complete (Transform2D, TransformSystem, basic SceneGraph utilities)
- ✅ World ECS system functional
- ✅ AssetManager functional
- ✅ EventBus functional (for future event integration)

---

## Step 2.1: SceneHandle

### Objective
Create a handle type for referencing scenes, following the same generational ID pattern as EntityId.

### File Structure
```
engine/core/scene/
└── SceneHandle.hpp
```

### Detailed Implementation Steps

#### Step 2.1.1: Create SceneHandle Header

**File:** `engine/core/scene/SceneHandle.hpp`

1. **Include necessary headers:**
   ```cpp
   #pragma once
   
   #include <cstdint>
   #include <functional>
   ```

2. **Define SceneHandle struct:**
   ```cpp
   /**
    * SceneHandle is a generational handle for referencing scenes.
    * Similar to EntityId, it uses an id and generation to prevent
    * use-after-destroy bugs.
    */
   struct SceneHandle {
       uint32_t id{0};
       uint32_t generation{0};
       
       /**
        * Checks if this handle is valid (non-zero ID).
        */
       bool isValid() const {
           return id != 0;
       }
       
       /**
        * Creates an invalid SceneHandle (used for "no scene").
        */
       static SceneHandle invalid() {
           return SceneHandle{0, 0};
       }
       
       // Equality operators
       bool operator==(const SceneHandle& other) const = default;
       bool operator!=(const SceneHandle& other) const {
           return !(*this == other);
       }
   };
   ```

3. **Add std::hash specialization for unordered_map support:**
   ```cpp
   namespace std {
       template<>
       struct hash<SceneHandle> {
           std::size_t operator()(const SceneHandle& handle) const noexcept {
               // Combine id and generation into a single hash
               return std::hash<uint64_t>{}(static_cast<uint64_t>(handle.generation) << 32 | handle.id);
           }
       };
   }
   ```

**Action Items:**
- [ ] Create `engine/core/scene/SceneHandle.hpp`
- [ ] Implement SceneHandle struct with id and generation
- [ ] Implement `isValid()` method
- [ ] Implement equality operators
- [ ] Implement std::hash specialization
- [ ] Verify compilation

#### Step 2.1.2: Test SceneHandle

**Create test file:** `tests/core/scene/SceneHandle_test.cpp`

```cpp
#include <gtest/gtest.h>
#include <unordered_map>
#include "SceneHandle.hpp"

TEST(SceneHandle, DefaultConstruction) {
    SceneHandle handle;
    EXPECT_FALSE(handle.isValid());
    EXPECT_EQ(handle.id, 0);
    EXPECT_EQ(handle.generation, 0);
}

TEST(SceneHandle, ValidHandle) {
    SceneHandle handle{1, 2};
    EXPECT_TRUE(handle.isValid());
    EXPECT_EQ(handle.id, 1);
    EXPECT_EQ(handle.generation, 2);
}

TEST(SceneHandle, InvalidStatic) {
    SceneHandle handle = SceneHandle::invalid();
    EXPECT_FALSE(handle.isValid());
}

TEST(SceneHandle, Equality) {
    SceneHandle a{1, 2};
    SceneHandle b{1, 2};
    SceneHandle c{2, 2};
    SceneHandle d{1, 3};
    
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, d);
}

TEST(SceneHandle, HashSupport) {
    SceneHandle a{1, 2};
    SceneHandle b{1, 2};
    SceneHandle c{2, 2};
    
    std::unordered_map<SceneHandle, int> map;
    map[a] = 10;
    map[c] = 20;
    
    EXPECT_EQ(map[a], 10);
    EXPECT_EQ(map[b], 10);  // Same hash
    EXPECT_EQ(map[c], 20);
}
```

**Action Items:**
- [ ] Write SceneHandle tests
- [ ] Run tests and verify all pass
- [ ] Verify hash works in unordered_map

---

## Step 2.2: Scene Class

### Objective
Create a Scene class that manages a collection of entities, tracks scene state, and provides entity lifecycle management within the scene context.

### File Structure
```
engine/core/scene/
├── Scene.hpp
└── Scene.cpp
```

### Detailed Implementation Steps

#### Step 2.2.1: Create Scene Header

**File:** `engine/core/scene/Scene.hpp`

1. **Include necessary headers:**
   ```cpp
   #pragma once
   
   #include <string>
   #include <unordered_set>
   #include <functional>
   #include <nlohmann/json.hpp>
   
   #include "../ecs/World.hpp"
   #include "../ecs/entity/EntityId.hpp"
   #include "SceneHandle.hpp"
   ```

2. **Define SceneState enum:**
   ```cpp
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
   ```

3. **Declare Scene class:**
   ```cpp
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
   ```

**Action Items:**
- [ ] Create `engine/core/scene/Scene.hpp`
- [ ] Define SceneState enum
- [ ] Declare Scene class with all methods
- [ ] Verify compilation

#### Step 2.2.2: Implement Scene Class

**File:** `engine/core/scene/Scene.cpp`

1. **Include headers:**
   ```cpp
   #include "Scene.hpp"
   #include "../scene/SceneGraph.hpp"
   #include <stdexcept>
   ```

2. **Implement constructor:**
   ```cpp
   Scene::Scene(World& world, const std::string& name)
       : _world(world), _name(name), _state(SceneState::Unloaded) {
       // Root entity will be created in onLoad()
   }
   ```

3. **Implement destructor:**
   ```cpp
   Scene::~Scene() {
       if (_state != SceneState::Unloaded) {
           onUnload();
       }
   }
   ```

4. **Implement lifecycle methods:**
   ```cpp
   void Scene::onLoad() {
       if (_state != SceneState::Unloaded) {
           throw std::runtime_error("Scene::onLoad() called on scene that is not Unloaded");
       }
       
       _state = SceneState::Loading;
       
       // Create invisible root entity
       _root = _world.createEntity();
       // Root has no components, is not tracked in _entities
       
       _state = SceneState::Active;
   }
   
   void Scene::onUnload() {
       if (_state == SceneState::Unloaded) {
           return;  // Already unloaded
       }
       
       _state = SceneState::Unloading;
       
       // Destroy all entities (this will also destroy children recursively)
       // Create a copy of entities set since we're modifying it
       auto entitiesCopy = _entities;
       for (EntityId id : entitiesCopy) {
           if (_world.isAlive(id)) {
               SceneGraph::destroyRecursive(_world, id);
           }
       }
       
       // Destroy root entity
       if (_world.isAlive(_root)) {
           _world.destroyEntity(_root);
       }
       
       // Clear tracking
       _entities.clear();
       _rootEntities.clear();
       _root = EntityId{0, 0};
       
       _state = SceneState::Unloaded;
   }
   
   void Scene::onActivate() {
       if (_state == SceneState::Unloaded) {
           throw std::runtime_error("Scene::onActivate() called on unloaded scene");
       }
       _state = SceneState::Active;
   }
   
   void Scene::onDeactivate() {
       if (_state == SceneState::Unloaded) {
           return;
       }
       _state = SceneState::Paused;
   }
   
   void Scene::onPause() {
       if (_state == SceneState::Unloaded) {
           return;
       }
       _state = SceneState::Paused;
   }
   
   void Scene::onResume() {
       if (_state == SceneState::Unloaded) {
           throw std::runtime_error("Scene::onResume() called on unloaded scene");
       }
       _state = SceneState::Active;
   }
   ```

5. **Implement entity management:**
   ```cpp
   EntityId Scene::createEntity() {
       return createEntity(_root);
   }
   
   EntityId Scene::createEntity(EntityId parent) {
       if (_state == SceneState::Unloaded) {
           throw std::runtime_error("Scene::createEntity() called on unloaded scene");
       }
       
       // Validate parent is in this scene (or is root)
       if (parent != _root) {
           if (!hasEntity(parent)) {
               throw std::runtime_error("Scene::createEntity() parent entity not in this scene");
           }
       }
       
       // Create entity in world
       EntityId entity = _world.createEntity();
       
       // Add to scene tracking
       addEntity(entity);
       
       // Set parent if not root
       if (parent != _root) {
           SceneGraph::setParent(_world, entity, parent);
       } else {
           // Make child of root
           SceneGraph::setParent(_world, entity, _root);
           _rootEntities.insert(entity);
       }
       
       return entity;
   }
   
   void Scene::destroyEntity(EntityId id) {
       if (!hasEntity(id)) {
           return;  // Not in this scene, ignore
       }
       
       // Remove from root entities if it's a root entity
       _rootEntities.erase(id);
       
       // Destroy recursively (handles children)
       SceneGraph::destroyRecursive(_world, id);
       
       // Remove from tracking
       removeEntity(id);
   }
   
   void Scene::reparent(EntityId entity, EntityId newParent) {
       if (!hasEntity(entity)) {
           throw std::runtime_error("Scene::reparent() entity not in this scene");
       }
       
       // Validate new parent
       if (newParent != _root && !hasEntity(newParent)) {
           throw std::runtime_error("Scene::reparent() new parent not in this scene");
       }
       
       // Get current parent
       EntityId oldParent = SceneGraph::getParent(_world, entity);
       
       // Update root entities tracking
       if (oldParent == _root) {
           _rootEntities.erase(entity);
       }
       if (newParent == _root) {
           _rootEntities.insert(entity);
       }
       
       // Reparent
       SceneGraph::setParent(_world, entity, newParent);
   }
   ```

6. **Implement helper methods:**
   ```cpp
   void Scene::addEntity(EntityId id) {
       _entities.insert(id);
   }
   
   void Scene::removeEntity(EntityId id) {
       _entities.erase(id);
       _rootEntities.erase(id);
   }
   ```

7. **Implement serialization (basic version):**
   ```cpp
   nlohmann::json Scene::serialize() const {
       nlohmann::json sceneJson;
       sceneJson["name"] = _name;
       
       nlohmann::json entitiesJson = nlohmann::json::array();
       
       // Serialize all entities (basic - full implementation in Phase 5)
       forEachEntity([&](EntityId id) {
           nlohmann::json entityJson;
           // TODO: Serialize entity components (Phase 5)
           entitiesJson.push_back(entityJson);
       });
       
       sceneJson["entities"] = entitiesJson;
       return sceneJson;
   }
   
   void Scene::deserialize(const nlohmann::json& data) {
       // Basic implementation - full version in Phase 5
       // This is a placeholder that will be implemented when SceneSerializer is created
       throw std::runtime_error("Scene::deserialize() not yet implemented - use SceneManager::loadScene()");
   }
   ```

**Action Items:**
- [ ] Create `engine/core/scene/Scene.cpp`
- [ ] Implement all methods
- [ ] Handle edge cases (unloaded scene operations, invalid entities)
- [ ] Verify compilation
- [ ] Test basic scene creation and entity management

#### Step 2.2.3: Update CMakeLists.txt

**File:** `engine/core/scene/CMakeLists.txt` (create if doesn't exist) or `engine/core/CMakeLists.txt`

Add Scene files to compilation:
```cmake
# If scene has its own CMakeLists.txt
set(SCENE_SOURCES
    Scene.cpp
    SceneGraph.cpp  # from Phase 1
)

set(SCENE_HEADERS
    Scene.hpp
    SceneHandle.hpp
    SceneGraph.hpp  # from Phase 1
)

# Or add to core/CMakeLists.txt
```

**Action Items:**
- [ ] Update CMakeLists.txt
- [ ] Build project
- [ ] Fix any compilation errors

#### Step 2.2.4: Test Scene Class

**Create test file:** `tests/core/scene/Scene_test.cpp`

Test cases:
1. Scene creation and destruction
2. Lifecycle state transitions
3. Entity creation (with and without parent)
4. Entity destruction
5. Entity reparenting
6. Root entity management
7. Root entities tracking
8. Iteration functions
9. Entity tracking consistency

**Action Items:**
- [ ] Write comprehensive Scene tests
- [ ] Run tests and verify all pass
- [ ] Test edge cases (operations on unloaded scene, etc.)

---

## Step 2.3: Additional SceneGraph Utilities

### Objective
Add path finding and tree traversal functions to the SceneGraph namespace (complementing Phase 1's hierarchy functions).

### File Structure
```
engine/core/scene/
├── SceneGraph.hpp (extend existing from Phase 1)
└── SceneGraph.cpp (extend existing from Phase 1)
```

### Detailed Implementation Steps

#### Step 2.3.1: Add Path Finding Function

**File:** `engine/core/scene/SceneGraph.hpp` (add to existing namespace)

Add declaration:
```cpp
namespace SceneGraph {
    // ... existing functions from Phase 1 ...
    
    /**
     * Finds an entity by path from a root entity.
     * Path format: "child/grandchild/greatgrandchild" or "/root/child"
     * 
     * @param world - The ECS world
     * @param root - Root entity to start search from
     * @param path - Path string (e.g., "enemy/sprite" or "/player/weapon")
     * @returns EntityId if found, invalid EntityId if not found
     * 
     * Example:
     *   EntityId player = findByPath(world, sceneRoot, "player");
     *   EntityId weapon = findByPath(world, sceneRoot, "player/weapon");
     */
    EntityId findByPath(World& world, EntityId root, std::string_view path);
}
```

**File:** `engine/core/scene/SceneGraph.cpp` (add implementation)

```cpp
#include <sstream>
#include <string>

EntityId SceneGraph::findByPath(World& world, EntityId root, std::string_view path) {
    if (!world.isAlive(root)) {
        return EntityId{0, 0};
    }
    
    // Handle absolute path (starts with '/')
    std::string_view searchPath = path;
    if (!searchPath.empty() && searchPath[0] == '/') {
        searchPath = searchPath.substr(1);  // Remove leading '/'
    }
    
    // Split path by '/'
    EntityId current = root;
    size_t start = 0;
    
    while (start < searchPath.length()) {
        size_t end = searchPath.find('/', start);
        if (end == std::string_view::npos) {
            end = searchPath.length();
        }
        
        std::string_view segment = searchPath.substr(start, end - start);
        if (segment.empty()) {
            start = end + 1;
            continue;
        }
        
        // Find child with matching name (via tag)
        // Note: This assumes entities have name tags
        // Alternative: Use a NameComponent if available
        bool found = false;
        auto children = getChildren(world, current);
        
        for (EntityId child : children) {
            // Check if child has a tag matching segment
            // For now, we'll need to check tags or use a different method
            // This is a simplified version - may need enhancement based on naming system
            
            // TODO: Implement name matching (may need NameComponent or tag system)
            // For now, return invalid - this will be enhanced when naming system is clear
        }
        
        if (!found) {
            return EntityId{0, 0};  // Path not found
        }
        
        start = end + 1;
    }
    
    return current;
}
```

**Note:** The `findByPath` implementation depends on how entities are named. If using tags, we can check tags. If using a NameComponent, we'd check that. For now, provide a skeleton that can be enhanced.

**Action Items:**
- [ ] Add `findByPath` declaration to SceneGraph.hpp
- [ ] Implement basic `findByPath` (may need to enhance based on naming system)
- [ ] Document naming requirements

#### Step 2.3.2: Add Tree Traversal Functions

**File:** `engine/core/scene/SceneGraph.hpp` (add to existing namespace)

Add declarations:
```cpp
namespace SceneGraph {
    // ... existing functions ...
    
    /**
     * Traverses the entity tree depth-first (pre-order).
     * Visits parent before children.
     * 
     * @param world - The ECS world
     * @param root - Root entity to start traversal from
     * @param fn - Function called for each entity: fn(EntityId)
     * 
     * Example:
     *   traverseDepthFirst(world, sceneRoot, [](EntityId id) {
     *       // Process entity
     *   });
     */
    template<typename Fn>
    void traverseDepthFirst(World& world, EntityId root, Fn&& fn);
    
    /**
     * Traverses the entity tree breadth-first (level-order).
     * Visits all entities at depth N before depth N+1.
     * 
     * @param world - The ECS world
     * @param root - Root entity to start traversal from
     * @param fn - Function called for each entity: fn(EntityId)
     * 
     * Example:
     *   traverseBreadthFirst(world, sceneRoot, [](EntityId id) {
     *       // Process entity
     *   });
     */
    template<typename Fn>
    void traverseBreadthFirst(World& world, EntityId root, Fn&& fn);
}
```

**File:** `engine/core/scene/SceneGraph.hpp` (implement templates in header)

Add implementations (templates must be in header):
```cpp
namespace SceneGraph {
    // ... existing code ...
    
    template<typename Fn>
    void traverseDepthFirst(World& world, EntityId root, Fn&& fn) {
        if (!world.isAlive(root)) {
            return;
        }
        
        // Visit current node
        fn(root);
        
        // Recursively visit children
        auto children = getChildren(world, root);
        for (EntityId child : children) {
            traverseDepthFirst(world, child, fn);
        }
    }
    
    template<typename Fn>
    void traverseBreadthFirst(World& world, EntityId root, Fn&& fn) {
        if (!world.isAlive(root)) {
            return;
        }
        
        std::queue<EntityId> queue;
        queue.push(root);
        
        while (!queue.empty()) {
            EntityId current = queue.front();
            queue.pop();
            
            if (!world.isAlive(current)) {
                continue;
            }
            
            // Visit current node
            fn(current);
            
            // Add children to queue
            auto children = getChildren(world, current);
            for (EntityId child : children) {
                queue.push(child);
            }
        }
    }
}
```

**Note:** Add `#include <queue>` to SceneGraph.hpp if not already present.

**Action Items:**
- [ ] Add traversal function declarations
- [ ] Implement template functions in header
- [ ] Add necessary includes (`<queue>` for breadth-first)
- [ ] Test traversal functions

#### Step 2.3.3: Test Additional Utilities

**Create test file:** `tests/core/scene/SceneGraph_traversal_test.cpp`

Test cases:
1. `traverseDepthFirst()` - Visits in correct order
2. `traverseBreadthFirst()` - Visits in level order
3. `findByPath()` - Finds entities by path (when naming is implemented)
4. Traversal with empty tree
5. Traversal with single node
6. Traversal with deep tree

**Action Items:**
- [ ] Write traversal tests
- [ ] Run tests and verify correctness
- [ ] Test edge cases

---

## Step 2.4: Basic SceneManager

### Objective
Create a SceneManager that loads scenes from JSON files, manages active scene, and migrates functionality from SceneLoader.

### File Structure
```
engine/core/scene/
├── SceneManager.hpp
└── SceneManager.cpp
```

### Detailed Implementation Steps

#### Step 2.4.1: Create SceneManager Header

**File:** `engine/core/scene/SceneManager.hpp`

1. **Include necessary headers:**
   ```cpp
   #pragma once
   
   #include <filesystem>
   #include <unordered_map>
   #include <memory>
   #include <string>
   
   #include "../assets/AssetManager.hpp"
   #include "../ecs/World.hpp"
   #include "../events/EventBus.hpp"
   #include "Scene.hpp"
   #include "SceneHandle.hpp"
   ```

2. **Declare SceneManager class:**
   ```cpp
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
   ```

**Action Items:**
- [ ] Create `engine/core/scene/SceneManager.hpp`
- [ ] Declare SceneManager class
- [ ] Verify compilation

#### Step 2.4.2: Implement SceneManager

**File:** `engine/core/scene/SceneManager.cpp`

1. **Include headers:**
   ```cpp
   #include "SceneManager.hpp"
   #include "../assets/RawAsset.hpp"
   #include <stdexcept>
   #include <nlohmann/json.hpp>
   ```

2. **Implement constructor:**
   ```cpp
   SceneManager::SceneManager(World& world, AssetManager& assets, EventBus& events)
       : _world(world), _assets(assets), _events(events) {
   }
   ```

3. **Implement destructor:**
   ```cpp
   SceneManager::~SceneManager() {
       unloadAllScenes();
   }
   ```

4. **Implement handle allocation:**
   ```cpp
   SceneHandle SceneManager::allocateHandle() {
       SceneHandle handle{_nextSceneId++, _sceneGeneration};
       return handle;
   }
   ```

5. **Implement loadScene():**
   ```cpp
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
   ```

6. **Implement unloadScene():**
   ```cpp
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
   ```

7. **Implement unloadAllScenes():**
   ```cpp
   void SceneManager::unloadAllScenes() {
       // Unload all scenes
       for (auto& [handle, scene] : _scenes) {
           scene->onUnload();
       }
       
       _scenes.clear();
       _activeScene = SceneHandle::invalid();
   }
   ```

8. **Implement query methods:**
   ```cpp
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
   ```

9. **Implement parseSceneJSON() (migrated from SceneLoader):**
   ```cpp
   void SceneManager::parseSceneJSON(Scene& scene, const nlohmann::json& sceneJson) {
       if (!sceneJson.contains("entities")) {
           return;  // No entities to create
       }
       
       // Map entity IDs (from JSON "id" field) to EntityIds
       std::unordered_map<std::string, EntityId> idMap;
       
       // First pass: Create all entities
       for (const auto& entityJson : sceneJson["entities"]) {
           createEntityFromJSON(scene, entityJson, idMap);
       }
       
       // Second pass: Set parent relationships
       // (This will be implemented in Step 2.5)
   }
   ```

10. **Implement createEntityFromJSON() (migrated from SceneLoader):**
    ```cpp
    void SceneManager::createEntityFromJSON(Scene& scene, const nlohmann::json& entityJson,
                                             std::unordered_map<std::string, EntityId>& idMap) {
        // Create entity in scene
        EntityId entity = scene.createEntity();
        
        // Store entity ID mapping if "id" field exists
        if (entityJson.contains("id")) {
            std::string id = entityJson["id"].get<std::string>();
            idMap[id] = entity;
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
                
                ComponentId componentId = maybeId.value();
                const ComponentInfo* info = _world.getRegistry().getInfo(componentId);
                if (info && info->deserializeFn) {
                    info->deserializeFn(_world, entity, componentData);
                }
            }
        }
    }
    ```

**Action Items:**
- [ ] Create `engine/core/scene/SceneManager.cpp`
- [ ] Implement all methods
- [ ] Migrate SceneLoader functionality
- [ ] Test scene loading
- [ ] Verify entities are created correctly

#### Step 2.4.3: Integrate SceneManager into Application

**File:** `engine/core/app/Application.hpp`

Replace SceneLoader with SceneManager:
```cpp
// Remove: SceneLoader _sceneLoader;
// Add:
#include "../scene/SceneManager.hpp"
SceneManager _sceneManager;
```

**File:** `engine/core/app/Application.cpp`

Update constructor:
```cpp
Application::Application(const std::filesystem::path& rootDir, const std::filesystem::path& manifestPath)
    : _manifestPath(manifestPath), 
      _rootDir(rootDir), 
      _assetManager(_rootDir), 
      _sceneManager(_ecsWorld, _assetManager, _eventBus) {
}
```

Update loadScenes():
```cpp
void Application::loadScenes() {
    JM_LOG_INFO("[Scene Loading]: {}", _manifest.entryScene);
    SceneHandle handle = _sceneManager.loadScene(_manifest.entryScene);
    Scene* scene = _sceneManager.getScene(handle);
    if (scene) {
        JM_LOG_INFO("[Scene Loaded]: {}", scene->getName());
    }
}
```

Add accessor:
```cpp
SceneManager& Application::getSceneManager() {
    return _sceneManager;
}
```

**Action Items:**
- [ ] Update Application.hpp to use SceneManager
- [ ] Update Application.cpp constructor and methods
- [ ] Remove SceneLoader (or keep for backward compatibility temporarily)
- [ ] Test application initialization
- [ ] Verify scenes load correctly

---

## Step 2.5: Update Scene JSON Format

### Objective
Enhance scene JSON format to support entity IDs and parent-child relationships, enabling hierarchical scene loading.

### Current JSON Format
```json
{
  "name": "Level 1",
  "entities": [
    {
      "name": "Player",
      "components": { ... }
    }
  ]
}
```

### Enhanced JSON Format
```json
{
  "name": "Level 1",
  "entities": [
    {
      "id": "player",
      "name": "Player",
      "parent": null,
      "components": {
        "Transform2D": {
          "position": [100, 200],
          "rotation": 0,
          "scale": [1, 1]
        },
        "ScriptComponent": {
          "script": "assets/scripts/player"
        }
      }
    },
    {
      "id": "player_sprite",
      "name": "PlayerSprite",
      "parent": "player",
      "components": {
        "Transform2D": {
          "position": [0, 0],
          "rotation": 0,
          "scale": [1, 1]
        },
        "SpriteComponent": {
          "texture": "assets/textures/player.png"
        }
      }
    }
  ]
}
```

### Detailed Implementation Steps

#### Step 2.5.1: Update parseSceneJSON() to Handle Hierarchy

**File:** `engine/core/scene/SceneManager.cpp`

Update `parseSceneJSON()` method:
```cpp
void SceneManager::parseSceneJSON(Scene& scene, const nlohmann::json& sceneJson) {
    if (!sceneJson.contains("entities")) {
        return;
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
```

**Action Items:**
- [ ] Update `parseSceneJSON()` to handle two-pass loading
- [ ] Implement parent relationship resolution
- [ ] Handle null parent (root entity)
- [ ] Handle missing parent IDs (log warning)
- [ ] Test with hierarchical JSON

#### Step 2.5.2: Update createEntityFromJSON() to Store IDs

**File:** `engine/core/scene/SceneManager.cpp`

The `createEntityFromJSON()` method already stores IDs in the map (from Step 2.4.2), but verify it handles the "id" field correctly:

```cpp
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
    
    // ... rest of method (name, components) ...
}
```

**Action Items:**
- [ ] Verify ID mapping works correctly
- [ ] Handle empty ID strings
- [ ] Handle duplicate IDs (log error, use first)

#### Step 2.5.3: Create Test Scene JSON

**File:** `demo_game/scenes/level1_hierarchical.scene.json` (or update existing)

Create a test scene with hierarchy:
```json
{
  "name": "Level 1 - Hierarchical",
  "entities": [
    {
      "id": "player",
      "name": "Player",
      "parent": null,
      "components": {
        "Transform2D": {
          "position": [100, 200],
          "rotation": 0,
          "scale": [1, 1]
        },
        "ScriptComponent": {
          "script": "assets/scripts/player"
        }
      }
    },
    {
      "id": "player_sprite",
      "name": "PlayerSprite",
      "parent": "player",
      "components": {
        "Transform2D": {
          "position": [0, 0],
          "rotation": 0,
          "scale": [1, 1]
        }
      }
    },
    {
      "id": "enemy",
      "name": "Enemy",
      "parent": null,
      "components": {
        "Transform2D": {
          "position": [500, 200],
          "rotation": 0,
          "scale": [1, 1]
        }
      }
    }
  ]
}
```

**Action Items:**
- [ ] Create test scene JSON with hierarchy
- [ ] Test loading the scene
- [ ] Verify parent-child relationships are established
- [ ] Verify transforms propagate correctly

#### Step 2.5.4: Test Hierarchy Loading

**Create test file:** `tests/integration/SceneHierarchyLoading_test.cpp`

Test cases:
1. Load scene with flat structure (no parents)
2. Load scene with simple parent-child (2 levels)
3. Load scene with deep hierarchy (3+ levels)
4. Load scene with multiple root entities
5. Load scene with missing parent ID (should handle gracefully)
6. Load scene with null parent (should be root)
7. Verify transforms propagate after loading

**Action Items:**
- [ ] Write hierarchy loading tests
- [ ] Run tests and verify all pass
- [ ] Test error cases (missing IDs, circular refs, etc.)

---

## Step 2.6: Integration & Testing

### Objective
Ensure Phase 2 integrates correctly with Phase 1 and existing systems, and all functionality works end-to-end.

### Integration Points

#### Step 2.6.1: Verify Phase 1 Integration

**Test that Scene uses Phase 1 components:**
- [ ] Scene entities can have Transform2D components
- [ ] Parent-child relationships use SceneGraph::setParent()
- [ ] TransformSystem updates scene entities correctly
- [ ] World matrices propagate through scene hierarchy

#### Step 2.6.2: Verify Application Integration

**Test Application with SceneManager:**
- [ ] Application initializes with SceneManager
- [ ] Scenes load from manifest entryScene
- [ ] Entities are created correctly
- [ ] No memory leaks when unloading scenes

#### Step 2.6.3: End-to-End Test

**Create comprehensive test:**
```cpp
TEST(SceneManager, EndToEndHierarchy) {
    // Setup
    World world;
    AssetManager assets("test_assets");
    EventBus events;
    SceneManager manager(world, assets, events);
    
    // Load scene with hierarchy
    SceneHandle handle = manager.loadScene("test_scene.json");
    Scene* scene = manager.getScene(handle);
    
    // Verify scene loaded
    ASSERT_NE(scene, nullptr);
    EXPECT_GT(scene->getEntityCount(), 0);
    
    // Verify hierarchy exists
    scene->forEachRootEntity([&](EntityId rootEntity) {
        auto children = SceneGraph::getChildren(world, rootEntity);
        EXPECT_GT(children.size(), 0);  // Has children
    });
    
    // Verify transforms work
    // (Run TransformSystem and check world matrices)
    
    // Unload scene
    manager.unloadScene(handle);
    EXPECT_EQ(scene->getEntityCount(), 0);
}
```

**Action Items:**
- [ ] Write end-to-end integration test
- [ ] Run test and verify all functionality
- [ ] Fix any integration issues

---

## Phase 2 Completion Checklist

### Functional Requirements
- [ ] SceneHandle created and works in hash maps
- [ ] Scene class manages entities correctly
- [ ] Scene lifecycle states work (Load, Unload, Activate, etc.)
- [ ] Root entity management works
- [ ] SceneGraph traversal functions work
- [ ] SceneManager loads scenes from JSON
- [ ] SceneManager tracks active scene
- [ ] JSON format supports entity IDs and parent relationships
- [ ] Hierarchical scenes load correctly
- [ ] All entities destroyed when scene unloads

### Code Quality
- [ ] Code compiles without warnings
- [ ] All functions documented
- [ ] Error handling implemented
- [ ] Memory safety verified (no leaks)
- [ ] Code follows project conventions

### Integration
- [ ] SceneManager integrates with Application
- [ ] Works with Phase 1 TransformSystem
- [ ] Works with existing ECS systems
- [ ] SceneLoader functionality migrated
- [ ] Ready for Phase 3 (advanced scene management)

### Testing
- [ ] SceneHandle tests pass
- [ ] Scene class tests pass
- [ ] SceneGraph traversal tests pass
- [ ] SceneManager tests pass
- [ ] Hierarchy loading tests pass
- [ ] Integration tests pass

---

## Estimated Time Breakdown

- **Step 2.1 (SceneHandle):** 1-2 hours
- **Step 2.2 (Scene Class):** 8-10 hours
- **Step 2.3 (Additional SceneGraph Utilities):** 4-5 hours
- **Step 2.4 (SceneManager):** 6-8 hours
- **Step 2.5 (JSON Format):** 4-5 hours
- **Step 2.6 (Integration & Testing):** 4-6 hours

**Total Phase 2:** 27-36 hours

---

## Common Pitfalls & Solutions

### Pitfall 1: Root Entity Not Created
**Issue:** Scene root entity not created in onLoad()
**Solution:** Always create root in onLoad(), verify it exists before creating entities

### Pitfall 2: Entity Tracking Inconsistency
**Issue:** Entities created but not tracked, or tracked but not in scene
**Solution:** Always call addEntity() when creating, removeEntity() when destroying

### Pitfall 3: Parent Resolution Failure
**Issue:** Parent ID in JSON doesn't match any entity ID
**Solution:** Build complete ID map in first pass, validate parent exists in second pass

### Pitfall 4: Scene State Machine Errors
**Issue:** Operations called on scene in wrong state
**Solution:** Validate state in each method, throw exceptions for invalid operations

### Pitfall 5: Memory Leaks on Unload
**Issue:** Entities not destroyed when scene unloads
**Solution:** Use destroyRecursive() to ensure all descendants are destroyed

---

## Dependencies on Phase 1

Phase 2 requires these Phase 1 components:
- ✅ Transform2D component
- ✅ TransformSystem
- ✅ SceneGraph::setParent()
- ✅ SceneGraph::getChildren()
- ✅ SceneGraph::destroyRecursive()
- ✅ SceneGraph::getParent()

If Phase 1 is not complete, Phase 2 cannot proceed.

---

## Next Steps After Phase 2

Once Phase 2 is complete:
1. ✅ Scene structure is in place
2. ✅ Scenes can be loaded with hierarchy
3. ✅ Ready for Phase 3 (scene stack, additive loading, state management)
4. ✅ Ready for Phase 5 (serialization - Scene class already has stubs)

**Phase 2 establishes the scene management foundation - ensure it's solid before moving to Phase 3!**
