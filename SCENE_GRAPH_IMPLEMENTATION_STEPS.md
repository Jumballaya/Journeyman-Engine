# Scene Graph Implementation Steps

## Overview

This document provides a detailed, step-by-step implementation plan for the Scene Graph & Scene Management System based on `SceneGraphPlan.md` and `SceneGraphWorkBreakdown.md`.

---

## Phase 1: Foundation - Transform Hierarchy

### Step 1.1: Transform2D Component

**Priority:** Critical  
**Estimated Time:** 2-3 hours  
**Dependencies:** None

#### Tasks:
1. Create `engine/core/components/Transform2D.hpp`
   - Define `Transform2D` struct extending `Component<Transform2D>`
   - Add `COMPONENT_NAME("Transform2D")` macro
   - Include local transform fields:
     - `glm::vec2 position{0.0f, 0.0f}`
     - `float rotation{0.0f}` (radians)
     - `glm::vec2 scale{1.0f, 1.0f}`
   - Include cached world transform:
     - `glm::mat3 localMatrix{1.0f}`
     - `glm::mat3 worldMatrix{1.0f}`
   - Include hierarchy fields:
     - `EntityId parent{0, 0}` (Invalid EntityId = root)
     - `std::vector<EntityId> children`
   - Include dirty tracking:
     - `bool dirty{true}`
     - `int32_t depth{0}` (0 = root)

2. Create `engine/core/components/Transform2D.cpp` (if needed for initialization)
   - Usually header-only for simple structs

3. Register component in `Application::registerScriptModule()` or similar:
   ```cpp
   _ecsWorld.registerComponent<Transform2D>(
       [](World& world, EntityId id, const nlohmann::json& json) {
           auto& transform = world.addComponent<Transform2D>(id);
           if (json.contains("position")) {
               auto pos = json["position"];
               transform.position = {pos[0], pos[1]};
           }
           if (json.contains("rotation")) {
               transform.rotation = json["rotation"].get<float>();
           }
           if (json.contains("scale")) {
               auto scl = json["scale"];
               transform.scale = {scl[0], scl[1]};
           }
       }
   );
   ```

4. Add to `CMakeLists.txt` if separate compilation unit

#### Verification:
- [ ] Component compiles
- [ ] Can create entity with Transform2D
- [ ] JSON deserialization works
- [ ] Can access position/rotation/scale

---

### Step 1.2: Transform System

**Priority:** Critical  
**Estimated Time:** 4-6 hours  
**Dependencies:** Transform2D component

#### Tasks:
1. Create `engine/core/systems/TransformSystem.hpp`
   - Define `TransformSystem` class extending `System`
   - Define system traits:
     ```cpp
     struct TransformUpdateTag {};
     using Provides = TypeList<TransformUpdateTag>;
     using Writes = TypeList<Transform2D>;
     using Reads = TypeList<>;
     using DependsOn = TypeList<>;  // Runs early
     ```
   - Declare `update(World& world, float dt)` override
   - Declare private helpers:
     - `void propagateTransforms(World& world)`
     - `void updateWorldMatrix(Transform2D& transform, const glm::mat3& parentWorld)`

2. Create `engine/core/systems/TransformSystem.cpp`
   - Implement `update()`:
     - Call `propagateTransforms()`
   - Implement `propagateTransforms()`:
     - Collect all entities with Transform2D component
     - Filter to dirty transforms only
     - Sort by depth (ensures parents update before children)
     - For each dirty transform:
       - Compute local matrix from position/rotation/scale
       - Get parent's world matrix (or identity if no parent)
       - Multiply: `worldMatrix = parentWorld * localMatrix`
       - Mark all children as dirty
       - Clear dirty flag
   - Implement `updateWorldMatrix()`:
     - Build local matrix from position/rotation/scale
     - Apply parent's world transform
     - Store result in worldMatrix

3. Matrix math helpers (glm usage):
   ```cpp
   // Local matrix
   glm::mat3 local = glm::mat3(1.0f);
   local = glm::translate(local, transform.position);
   local = glm::rotate(local, transform.rotation);
   local = glm::scale(local, transform.scale);
   
   // World matrix
   transform.worldMatrix = parentWorld * local;
   ```

4. Register system in `Application`:
   ```cpp
   _ecsWorld.registerSystem<TransformSystem>();
   ```

5. Update CMakeLists.txt

#### Verification:
- [ ] System compiles and registers
- [ ] Local matrices compute correctly
- [ ] World matrices update when parent moves
- [ ] Dirty flag propagation works
- [ ] Depth sorting ensures correct order

---

### Step 1.3: Hierarchy Manipulation Utilities

**Priority:** High  
**Estimated Time:** 3-4 hours  
**Dependencies:** Transform2D, TransformSystem

#### Tasks:
1. Add to `World.hpp` or create `SceneGraph.hpp`:
   ```cpp
   // In World class or SceneGraph namespace
   void setParent(EntityId child, EntityId parent);
   std::vector<EntityId> getChildren(EntityId entity) const;
   void removeFromParent(EntityId entity);
   ```

2. Implement `setParent()`:
   - Validate both entities are alive
   - Check for circular dependency (child not ancestor of parent)
   - Remove child from old parent's children list
   - Add child to new parent's children list
   - Update child's parent field
   - Update depth field (recursively update children's depths)
   - Mark child and all descendants as dirty

3. Implement `getChildren()`:
   - Get Transform2D component
   - Return copy of children vector

4. Implement `removeFromParent()`:
   - Call `setParent(child, invalidEntityId)` to make root

5. Implement depth calculation:
   - When setting parent, compute depth = parent.depth + 1
   - Recursively update all descendants

6. Circular dependency check:
   - Traverse up parent chain from parent
   - Ensure child is not an ancestor

#### Verification:
- [ ] Can set parent-child relationships
- [ ] Circular dependencies are prevented
- [ ] Depth updates correctly
- [ ] Children lists maintain consistency
- [ ] Removing parent makes entity root

---

### Step 1.4: Unit Tests for Phase 1

**Priority:** High  
**Estimated Time:** 4-5 hours  
**Dependencies:** All Phase 1 components

#### Tasks:
1. Transform2D component tests:
   - Default values are correct
   - JSON deserialization
   - Field access/modification

2. Transform matrix math tests:
   - Local matrix computation
   - World matrix with identity parent
   - World matrix with translated parent
   - World matrix with rotated parent
   - World matrix with scaled parent
   - Nested transforms (grandparent → parent → child)

3. Hierarchy operation tests:
   - `setParent()` basic functionality
   - `setParent()` prevents circular dependency
   - `getChildren()` returns correct list
   - `removeFromParent()` makes root
   - Depth calculation correctness

4. TransformSystem tests:
   - Dirty flag propagation
   - Depth-based sorting
   - Update order (parents before children)
   - Performance: 10,000 entities

#### Test Files:
- `tests/core/components/Transform2D_test.cpp`
- `tests/core/systems/TransformSystem_test.cpp`
- `tests/core/scene/Hierarchy_test.cpp`

---

## Phase 2: Scene Structure

### Step 2.1: SceneHandle

**Priority:** Critical  
**Estimated Time:** 1 hour  
**Dependencies:** None

#### Tasks:
1. Create `engine/core/scene/SceneHandle.hpp`
   - Define `SceneHandle` struct:
     ```cpp
     struct SceneHandle {
         uint32_t id{0};
         uint32_t generation{0};
         
         bool isValid() const { return id != 0; }
         bool operator==(const SceneHandle&) const = default;
     };
     ```
   - Add std::hash specialization for unordered_map support

#### Verification:
- [ ] Compiles
- [ ] Can use in hash map
- [ ] Equality comparison works

---

### Step 2.2: Scene Class (Basic)

**Priority:** Critical  
**Estimated Time:** 6-8 hours  
**Dependencies:** SceneHandle, World

#### Tasks:
1. Create `engine/core/scene/Scene.hpp`
   - Define `SceneState` enum:
     ```cpp
     enum class SceneState {
         Unloaded,
         Loading,
         Active,
         Paused,
         Unloading
     };
     ```
   - Declare Scene class:
     - Private members:
       - `std::string _name`
       - `SceneState _state{SceneState::Unloaded}`
       - `EntityId _root` (invisible root node for hierarchy)
       - `std::unordered_set<EntityId> _entities`
       - `std::unordered_set<EntityId> _rootEntities`
       - `World& _world` (reference)

2. Create `engine/core/scene/Scene.cpp`
   - Constructor:
     - Take World reference and scene name
     - Create root entity (invisible, no Transform2D)
     - Initialize state to Unloaded
   - Entity management:
     - `EntityId createEntity()` - create and add to _entities
     - `EntityId createEntity(EntityId parent)` - create with parent
     - `void destroyEntity(EntityId id)` - destroy and remove from tracking
     - `void reparent(EntityId entity, EntityId newParent)`
   - Lifecycle methods (stubs for now):
     - `onLoad()`, `onUnload()`, `onActivate()`, `onDeactivate()`, `onPause()`, `onResume()`
   - Query methods:
     - `EntityId getRoot() const`
     - `const std::string& getName() const`
     - `SceneState getState() const`
   - Iteration:
     - `template<typename Fn> void forEachEntity(Fn&& fn)`
     - `template<typename Fn> void forEachRootEntity(Fn&& fn)`

3. Root entity management:
   - Create invisible root entity in constructor
   - All scene entities are children of root (by default)
   - Root never has components or is visible

4. Update CMakeLists.txt

#### Verification:
- [ ] Scene creates and tracks entities
- [ ] Root entity exists and works
- [ ] Entity tracking maintains consistency
- [ ] Iteration functions work

---

### Step 2.3: SceneGraph Utilities

**Priority:** High  
**Estimated Time:** 8-10 hours  
**Dependencies:** Scene, Transform2D

#### Tasks:
1. Create `engine/core/scene/SceneGraph.hpp`
   - Declare namespace `SceneGraph`
   - Hierarchy manipulation:
     - `void setParent(World& world, EntityId child, EntityId parent)`
     - `void removeFromParent(World& world, EntityId entity)`
     - `void destroyRecursive(World& world, EntityId entity)`
   - Queries:
     - `EntityId getParent(World& world, EntityId entity)`
     - `std::vector<EntityId> getChildren(World& world, EntityId entity)`
     - `std::vector<EntityId> getAncestors(World& world, EntityId entity)`
     - `std::vector<EntityId> getDescendants(World& world, EntityId entity)`
     - `EntityId findByPath(World& world, EntityId root, std::string_view path)` (e.g., "root/enemy/sprite")
   - Transform utilities:
     - `glm::vec2 getWorldPosition(World& world, EntityId entity)`
     - `void setWorldPosition(World& world, EntityId entity, glm::vec2 pos)`
     - `glm::vec2 localToWorld(World& world, EntityId entity, glm::vec2 localPos)`
     - `glm::vec2 worldToLocal(World& world, EntityId entity, glm::vec2 worldPos)`
   - Traversal:
     - `template<typename Fn> void traverseDepthFirst(World& world, EntityId root, Fn&& fn)`
     - `template<typename Fn> void traverseBreadthFirst(World& world, EntityId root, Fn&& fn)`

2. Create `engine/core/scene/SceneGraph.cpp`
   - Implement hierarchy functions (delegate to World methods or implement here)
   - Implement query functions using Transform2D component
   - Implement transform utilities using worldMatrix
   - Implement traversal functions (recursive or iterative)

#### Verification:
- [ ] All utility functions work correctly
- [ ] Path finding works ("root/child/grandchild")
- [ ] World/local coordinate conversions accurate
- [ ] Traversal functions visit all nodes

---

### Step 2.4: Basic SceneManager

**Priority:** Critical  
**Estimated Time:** 6-8 hours  
**Dependencies:** Scene, SceneHandle

#### Tasks:
1. Create `engine/core/scene/SceneManager.hpp`
   - Declare SceneManager class:
     - Private members:
       - `World& _world`
       - `AssetManager& _assets`
       - `EventBus& _events`
       - `std::unordered_map<SceneHandle, std::unique_ptr<Scene>> _scenes`
       - `SceneHandle _activeScene`
       - `uint32_t _nextSceneId{1}`
       - `uint32_t _sceneGeneration{0}`
     - Public methods:
       - Constructor taking World, AssetManager, EventBus
       - `SceneHandle loadScene(const std::filesystem::path& path)`
       - `Scene* getScene(SceneHandle handle)`
       - `Scene* getActiveScene()`
       - `SceneHandle getActiveSceneHandle() const`
       - `SceneHandle allocateHandle()` private helper

2. Create `engine/core/scene/SceneManager.cpp`
   - Implement constructor
   - Implement `allocateHandle()`:
     - Return SceneHandle{_nextSceneId++, _sceneGeneration}
   - Implement `loadScene()`:
     - Allocate handle
     - Load JSON from AssetManager
     - Create Scene instance
     - Parse JSON and create entities (similar to SceneLoader)
     - Set as active scene
     - Store in _scenes map
     - Return handle
   - Implement `getScene()` and `getActiveScene()`

3. Migrate SceneLoader functionality:
   - Move entity creation logic to SceneManager
   - Support basic scene JSON format
   - Handle component registration

4. Update CMakeLists.txt

#### Verification:
- [ ] Can load scene from JSON
- [ ] Entities are created correctly
- [ ] Scene tracking works
- [ ] Can retrieve active scene

---

### Step 2.5: Update Scene JSON Format

**Priority:** High  
**Estimated Time:** 3-4 hours  
**Dependencies:** SceneManager

#### Tasks:
1. Update scene JSON parsing to support:
   - Entity "id" field (string identifier)
   - Entity "parent" field (string reference or null)
   - Entity "children" array (array of string IDs)
   
2. Parse hierarchy during load:
   - First pass: Create all entities, build ID→EntityId map
   - Second pass: Set parent relationships using ID map

3. Example JSON format:
   ```json
   {
     "name": "Level 1",
     "entities": [
       {
         "id": "player",
         "parent": null,
         "components": {
           "Transform2D": {"position": [100, 200]}
         }
       },
       {
         "id": "player_sprite",
         "parent": "player",
         "components": {
           "Transform2D": {"position": [0, 0]}
         }
       }
     ]
   }
   ```

#### Verification:
- [ ] JSON with hierarchy loads correctly
- [ ] Parent-child relationships established
- [ ] Transform hierarchy works end-to-end

---

## Phase 3: Scene Manager Features

### Step 3.1: Scene Stack

**Priority:** High  
**Estimated Time:** 4-5 hours  
**Dependencies:** Basic SceneManager

#### Tasks:
1. Add to SceneManager:
   - `std::vector<SceneHandle> _sceneStack`
   - `void pushScene(const std::filesystem::path& path)`
   - `void popScene()`
   - `void loadSceneAdditive(const std::filesystem::path& path)`

2. Implement `pushScene()`:
   - Load scene (or get existing)
   - Push to stack
   - Activate (top of stack is active)

3. Implement `popScene()`:
   - Pop from stack
   - Deactivate popped scene
   - Activate new top scene (if any)

4. Implement `loadSceneAdditive()`:
   - Load scene without unloading current
   - Add to stack
   - Don't deactivate current scene

5. Update scene activation logic:
   - Only top of stack is truly active
   - Others can be paused/background

#### Verification:
- [ ] Can push/pop scenes
- [ ] Stack maintains correct order
- [ ] Top scene is always active
- [ ] Multiple scenes can coexist

---

### Step 3.2: Scene State Management

**Priority:** High  
**Estimated Time:** 4-5 hours  
**Dependencies:** Scene, SceneManager

#### Tasks:
1. Implement Scene lifecycle methods:
   - `onLoad()` - Set state to Loading, then Active
   - `onUnload()` - Set state to Unloading, then Unloaded
   - `onActivate()` - Set state to Active
   - `onDeactivate()` - Set state to Paused
   - `onPause()` - Set state to Paused
   - `onResume()` - Set state to Active

2. Add to SceneManager:
   - `void pauseScene(SceneHandle handle)`
   - `void resumeScene(SceneHandle handle)`
   - `void setActiveScene(SceneHandle handle)`

3. Implement state transitions:
   - Validate state machine (can't go from Unloaded to Paused, etc.)
   - Emit state change events (next step)

#### Verification:
- [ ] State transitions are valid
- [ ] Lifecycle methods work correctly
- [ ] State queries return correct values

---

### Step 3.3: Scene Events

**Priority:** Medium  
**Estimated Time:** 3-4 hours  
**Dependencies:** SceneManager, EventBus

#### Tasks:
1. Create `engine/core/scene/SceneEvents.hpp`
   - Define event structs:
     ```cpp
     namespace events {
         struct SceneLoadStarted { SceneHandle handle; std::string sceneName; };
         struct SceneLoadCompleted { SceneHandle handle; std::string sceneName; };
         struct SceneUnloadStarted { SceneHandle handle; std::string sceneName; };
         struct SceneUnloaded { SceneHandle handle; std::string sceneName; };
         struct SceneActivated { SceneHandle handle; std::string sceneName; };
         struct SceneDeactivated { SceneHandle handle; std::string sceneName; };
         struct SceneTransitionStarted { SceneHandle from; SceneHandle to; std::string transitionType; };
         struct SceneTransitionCompleted { SceneHandle from; SceneHandle to; };
     }
     ```

2. Update `engine/core/events/Event.hpp`:
   - Add scene events to Event variant

3. Emit events from SceneManager:
   - `SceneLoadStarted` when starting load
   - `SceneLoadCompleted` when finished
   - Similar for other operations

#### Verification:
- [ ] Events are emitted at correct times
- [ ] Event subscribers receive notifications
- [ ] Event data is correct

---

### Step 3.4: Scene Manager Update Loop

**Priority:** Medium  
**Estimated Time:** 2-3 hours  
**Dependencies:** SceneManager

#### Tasks:
1. Add `void update(float dt)` to SceneManager
   - Handle any frame-by-frame updates
   - Process scene transitions (future)
   - Handle auto-save timer (future)

2. Integrate into Application:
   ```cpp
   void Application::run() {
       // ...
       while (_running) {
           // ...
           _sceneManager.update(dt);
           // ...
       }
   }
   ```

#### Verification:
- [ ] Update loop runs every frame
- [ ] No performance issues

---

## Phase 4: Scene Transitions (Defer if needed)

### Step 4.1-4.7: Transition System

**Priority:** Medium  
**Estimated Time:** 12-15 hours  
**Dependencies:** SceneManager, Renderer2D

#### Implementation Notes:
- See SceneGraphPlan.md Phase 4 for detailed specs
- Start with FadeTransition (simplest)
- Add SlideTransition and CrossFadeTransition
- ShaderTransition can be placeholder for future

---

## Phase 5: Serialization & Save System

### Step 5.1: Component Serialization Registration

**Priority:** High  
**Estimated Time:** 4-5 hours  
**Dependencies:** ComponentRegistry

#### Tasks:
1. Update `ComponentInfo` struct:
   - Add `std::function<nlohmann::json(World&, EntityId)> serializeFn`

2. Update `World::registerComponent()`:
   - Accept serializer function parameter
   - Store in ComponentInfo

3. Update existing component registrations:
   - Add serializer lambdas for Transform2D, SpriteComponent, etc.

#### Verification:
- [ ] Components can serialize to JSON
- [ ] Serialization round-trips correctly

---

### Step 5.2: SceneSerializer

**Priority:** High  
**Estimated Time:** 8-10 hours  
**Dependencies:** Component serialization

#### Tasks:
1. Create `engine/core/scene/SceneSerializer.hpp` and `.cpp`
2. Implement serialization:
   - `serializeScene()` - Convert Scene to JSON
   - `serializeEntity()` - Convert EntityId to JSON
   - `serializeComponent()` - Use registered serializers
3. Implement deserialization:
   - `deserializeScene()` - Load from JSON
   - `deserializeEntity()` - Create from JSON
   - `deserializeComponent()` - Use registered deserializers
4. Handle entity ID mapping for references

#### Verification:
- [ ] Save scene → load scene → identical state
- [ ] Hierarchy preserved
- [ ] All components serialized

---

### Step 5.3-5.7: SaveManager

**Priority:** Medium  
**Estimated Time:** 10-12 hours  
**Dependencies:** SceneSerializer

#### Implementation Notes:
- See SceneGraphPlan.md Phase 6 for detailed specs
- Start with basic save/load
- Add persistent state system
- Auto-save can be added later

---

## Phase 6: Integration & Polish

### Step 6.1-6.8: Full Integration

**Priority:** High  
**Estimated Time:** 10-15 hours  
**Dependencies:** All previous phases

#### Tasks:
- Replace SceneLoader with SceneManager in Application
- Add SaveManager to Application
- Update renderer for scene-based rendering
- Write comprehensive tests
- Performance profiling
- Documentation

---

## Implementation Order Summary

### Must Have (MVP):
1. ✅ Phase 1: Transform Hierarchy (Foundation)
2. ✅ Phase 2: Scene Structure (Basic)
3. ✅ Phase 3.1-3.2: Scene Stack & State (Core features)
4. ✅ Phase 5.1-5.2: Basic Serialization

### Should Have:
5. Phase 3.3-3.4: Events & Update Loop
6. Phase 5.3-5.7: SaveManager (basic)
7. Phase 6: Integration

### Nice to Have:
8. Phase 4: Scene Transitions
9. Phase 5: Full Save System
10. Phase 6: Polish & Performance

---

## Estimated Total Time

- Phase 1: 13-18 hours
- Phase 2: 20-27 hours
- Phase 3: 13-17 hours
- Phase 4: 12-15 hours (optional)
- Phase 5: 22-27 hours
- Phase 6: 10-15 hours

**Total MVP:** ~66-89 hours  
**Total Full:** ~90-119 hours

---

## Critical Path Dependencies

```
Transform2D → TransformSystem → Hierarchy Utilities
                ↓
            Scene → SceneManager → Scene Stack
                ↓
            Serialization → SaveManager
```

Start with Phase 1, as everything else depends on transform hierarchy.
