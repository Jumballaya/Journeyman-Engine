# Phase 1: Transform Hierarchy - Comprehensive Implementation Breakdown

## Overview

Phase 1 establishes the foundation for the scene graph system by implementing:
1. **Transform2D Component** - Stores local transform data, cached world matrices, and hierarchy relationships
2. **TransformSystem** - Propagates transforms through parent-child hierarchies, computing world matrices
3. **Hierarchy Manipulation Utilities** - Functions to establish and query parent-child relationships
4. **Comprehensive Testing** - Unit tests, integration tests, and performance benchmarks

**Goal:** Enable entities to have hierarchical parent-child relationships where child transforms automatically update when parents move, rotate, or scale.

**Success Criteria:**
- ✅ Entities can have parent-child relationships
- ✅ Child world transforms update when parent transforms change
- ✅ Transform propagation is efficient (dirty flag system)
- ✅ Hierarchy operations (setParent, getChildren, removeFromParent) work correctly
- ✅ System handles 10,000+ entities at 60 FPS

---

## Prerequisites

Before starting Phase 1, ensure you have:
- [x] GLM library integrated (for `glm::vec2`, `glm::mat3`)
- [x] ECS system working (World, Component, System)
- [x] Component registration system functional
- [x] System registration and scheduling working
- [x] Basic understanding of matrix math for 2D transforms

---

## Step 1.1: Transform2D Component

### Objective
Create a component that stores an entity's local transform (position, rotation, scale), cached world transform matrices, hierarchy relationships, and dirty tracking.

### File Structure
```
engine/core/components/
├── Transform2D.hpp
└── Transform2D.cpp (optional, if needed)
```

### Detailed Implementation Steps

#### Step 1.1.1: Create Component Header

**File:** `engine/core/components/Transform2D.hpp`

1. **Include necessary headers:**
   ```cpp
   #pragma once
   
   #include <vector>
   #include <cstdint>
   #include <glm/glm.hpp>
   #include <glm/gtc/matrix_transform.hpp>
   
   #include "../ecs/component/Component.hpp"
   #include "../ecs/entity/EntityId.hpp"
   ```

2. **Define the Transform2D struct:**
   ```cpp
   struct Transform2D : public Component<Transform2D> {
       COMPONENT_NAME("Transform2D");
       
       // Local transform (relative to parent)
       glm::vec2 position{0.0f, 0.0f};
       float rotation{0.0f};           // radians, counter-clockwise
       glm::vec2 scale{1.0f, 1.0f};
       
       // Cached world transform matrices
       glm::mat3 localMatrix{1.0f};    // Computed from position/rotation/scale
       glm::mat3 worldMatrix{1.0f};    // parentWorld * localMatrix
       
       // Hierarchy relationships
       EntityId parent{0, 0};          // Invalid EntityId = root (no parent)
       std::vector<EntityId> children; // Direct children only
       
       // Dirty tracking for efficient updates
       bool dirty{true};               // true = needs world matrix recomputation
       int32_t depth{0};               // 0 = root, increases with each level
       
       // Default constructor
       Transform2D() = default;
       
       // Convenience constructor
       Transform2D(glm::vec2 pos, float rot = 0.0f, glm::vec2 scl = {1.0f, 1.0f})
           : position(pos), rotation(rot), scale(scl), dirty(true) {}
   };
   ```

3. **Design decisions:**
   - Use `glm::mat3` for 2D transforms (3x3 matrix handles translation via homogeneous coordinates)
   - Store both local and world matrices to avoid recomputation
   - Use `std::vector<EntityId>` for children (cache-friendly for iteration)
   - Depth field enables topological sort without tree traversal
   - Invalid EntityId (0,0) represents "no parent" (root entity)

#### Step 1.1.2: Verify Component Compilation

**Action Items:**
- [ ] Create `Transform2D.hpp` with above structure
- [ ] Include in a test file and verify it compiles
- [ ] Verify GLM integration works (check glm::vec2, glm::mat3 available)

**Verification:**
```cpp
// Quick test in a temporary file
#include "Transform2D.hpp"
Transform2D t;
t.position = {10.0f, 20.0f};
// Should compile without errors
```

#### Step 1.1.3: Register Component in World

**File:** `engine/core/app/Application.cpp` (or appropriate initialization location)

1. **Add component registration:**
   ```cpp
   void Application::registerTransformComponent() {
       _ecsWorld.registerComponent<Transform2D>(
           [](World& world, EntityId id, const nlohmann::json& json) {
               auto& transform = world.addComponent<Transform2D>(id);
               
               // Parse position
               if (json.contains("position")) {
                   auto pos = json["position"];
                   if (pos.is_array() && pos.size() >= 2) {
                       transform.position = {pos[0].get<float>(), pos[1].get<float>()};
                   }
               }
               
               // Parse rotation (in degrees, convert to radians)
               if (json.contains("rotation")) {
                   float degrees = json["rotation"].get<float>();
                   transform.rotation = glm::radians(degrees);
               }
               
               // Parse scale
               if (json.contains("scale")) {
                   auto scl = json["scale"];
                   if (scl.is_array() && scl.size() >= 2) {
                       transform.scale = {scl[0].get<float>(), scl[1].get<float>()};
                   } else if (scl.is_number()) {
                       float uniform = scl.get<float>();
                       transform.scale = {uniform, uniform};
                   }
               }
               
               // Mark as dirty to trigger initial world matrix computation
               transform.dirty = true;
           }
       );
   }
   ```

2. **Call registration in Application::initialize():**
   ```cpp
   void Application::initialize() {
       // ... existing code ...
       registerTransformComponent();
       // ... rest of initialization ...
   }
   ```

**Action Items:**
- [ ] Add `registerTransformComponent()` method to Application
- [ ] Call it in `initialize()`
- [ ] Test JSON deserialization with sample JSON

**Test JSON Example:**
```json
{
  "position": [100.0, 200.0],
  "rotation": 45.0,
  "scale": [1.5, 1.5]
}
```

#### Step 1.1.4: Update CMakeLists.txt

**File:** `engine/core/components/CMakeLists.txt` (create if doesn't exist, or add to `engine/core/CMakeLists.txt`)

Add Transform2D to compilation:
```cmake
# If components have their own CMakeLists.txt
set(COMPONENT_SOURCES
    Transform2D.cpp  # if separate implementation file
)

# Or add to core/CMakeLists.txt
# Add Transform2D.hpp to headers list
```

**Action Items:**
- [ ] Verify CMakeLists.txt includes component files
- [ ] Build project to ensure no compilation errors
- [ ] Fix any include path issues

#### Step 1.1.5: Basic Component Access Test

**Create test file:** `tests/core/components/Transform2D_basic_test.cpp` (or similar)

```cpp
#include <gtest/gtest.h>  // or your test framework
#include "Transform2D.hpp"
#include "World.hpp"

TEST(Transform2D, BasicCreation) {
    World world;
    EntityId entity = world.createEntity();
    
    auto& transform = world.addComponent<Transform2D>(entity);
    transform.position = {10.0f, 20.0f};
    transform.rotation = 1.57f;  // ~90 degrees
    transform.scale = {2.0f, 2.0f};
    
    EXPECT_EQ(transform.position.x, 10.0f);
    EXPECT_EQ(transform.position.y, 20.0f);
    EXPECT_EQ(transform.rotation, 1.57f);
    EXPECT_EQ(transform.scale.x, 2.0f);
    EXPECT_TRUE(transform.dirty);
    EXPECT_EQ(transform.depth, 0);
}
```

**Action Items:**
- [ ] Write basic component creation test
- [ ] Run test to verify component works
- [ ] Test component retrieval: `world.getComponent<Transform2D>(entity)`

---

## Step 1.2: TransformSystem

### Objective
Create a system that efficiently propagates transforms through the hierarchy, computing world matrices from local transforms and parent world matrices.

### File Structure
```
engine/core/systems/
├── TransformSystem.hpp
└── TransformSystem.cpp
```

### Detailed Implementation Steps

#### Step 1.2.1: Create System Header

**File:** `engine/core/systems/TransformSystem.hpp`

1. **Include necessary headers:**
   ```cpp
   #pragma once
   
   #include "../ecs/system/System.hpp"
   #include "../ecs/system/SystemTraits.hpp"
   #include "../ecs/system/TypeList.hpp"
   #include "../components/Transform2D.hpp"
   #include <glm/glm.hpp>
   #include <glm/gtc/matrix_transform.hpp>
   ```

2. **Define system traits:**
   ```cpp
   // Tag type for systems that depend on transform updates
   struct TransformUpdateTag {};
   
   template<>
   struct SystemTraits<TransformSystem> {
       using Provides = TypeList<TransformUpdateTag>;
       using Writes = TypeList<Transform2D>;
       using Reads = TypeList<>;
       using DependsOn = TypeList<>;  // Runs early, no dependencies
   };
   ```

3. **Declare TransformSystem class:**
   ```cpp
   class TransformSystem : public System {
   public:
       TransformSystem() = default;
       ~TransformSystem() = default;
       
       void update(World& world, float dt) override;
       
       const char* name() const override { return "TransformSystem"; }
       
   private:
       // Main propagation algorithm
       void propagateTransforms(World& world);
       
       // Update a single transform's world matrix
       void updateWorldMatrix(World& world, Transform2D& transform, EntityId entity);
       
       // Compute local matrix from position/rotation/scale
       glm::mat3 computeLocalMatrix(const Transform2D& transform) const;
       
       // Get parent's world matrix (or identity if no parent)
       glm::mat3 getParentWorldMatrix(World& world, EntityId parent) const;
   };
   ```

#### Step 1.2.2: Implement System Logic

**File:** `engine/core/systems/TransformSystem.cpp`

1. **Implement update() method:**
   ```cpp
   #include "TransformSystem.hpp"
   #include "../ecs/World.hpp"
   #include "../ecs/View.hpp"
   
   void TransformSystem::update(World& world, float dt) {
       (void)dt;  // Delta time not used for transform updates
       
       if (!enabled) {
           return;
       }
       
       propagateTransforms(world);
   }
   ```

2. **Implement propagateTransforms() - Core Algorithm:**
   ```cpp
   void TransformSystem::propagateTransforms(World& world) {
       // Step 1: Collect all entities with Transform2D component
       auto view = world.view<Transform2D>();
       
       // Step 2: Filter to only dirty transforms
       std::vector<std::pair<EntityId, Transform2D*>> dirtyTransforms;
       for (auto [entity, transform] : view) {
           if (transform->dirty) {
               dirtyTransforms.push_back({entity, transform});
           }
       }
       
       // Step 3: Sort by depth (ensures parents update before children)
       std::sort(dirtyTransforms.begin(), dirtyTransforms.end(),
           [](const auto& a, const auto& b) {
               return a.second->depth < b.second->depth;
           });
       
       // Step 4: Update each dirty transform
       for (auto [entity, transform] : dirtyTransforms) {
           updateWorldMatrix(world, *transform, entity);
           
           // Step 5: Mark all children as dirty (they need to recompute)
           for (EntityId child : transform->children) {
               if (world.isAlive(child)) {
                   auto* childTransform = world.getComponent<Transform2D>(child);
                   if (childTransform) {
                       childTransform->dirty = true;
                   }
               }
           }
           
           // Step 6: Clear dirty flag
           transform->dirty = false;
       }
   }
   ```

3. **Implement updateWorldMatrix():**
   ```cpp
   void TransformSystem::updateWorldMatrix(World& world, Transform2D& transform, EntityId entity) {
       // Compute local matrix from position/rotation/scale
       transform.localMatrix = computeLocalMatrix(transform);
       
       // Get parent's world matrix (or identity if no parent)
       glm::mat3 parentWorld = getParentWorldMatrix(world, transform.parent);
       
       // World = parentWorld * localMatrix
       transform.worldMatrix = parentWorld * transform.localMatrix;
   }
   ```

4. **Implement computeLocalMatrix():**
   ```cpp
   glm::mat3 TransformSystem::computeLocalMatrix(const Transform2D& transform) const {
       // Start with identity matrix
       glm::mat3 matrix{1.0f};
       
       // Apply translation
       matrix = glm::translate(matrix, transform.position);
       
       // Apply rotation (around origin)
       matrix = glm::rotate(matrix, transform.rotation);
       
       // Apply scale
       matrix = glm::scale(matrix, transform.scale);
       
       return matrix;
   }
   ```

5. **Implement getParentWorldMatrix():**
   ```cpp
   glm::mat3 TransformSystem::getParentWorldMatrix(World& world, EntityId parent) const {
       // Invalid parent = root entity, use identity
       if (!world.isAlive(parent)) {
           return glm::mat3{1.0f};
       }
       
       auto* parentTransform = world.getComponent<Transform2D>(parent);
       if (!parentTransform) {
           // Parent exists but has no transform, use identity
           return glm::mat3{1.0f};
       }
       
       // If parent is dirty, we have a problem (shouldn't happen due to sorting)
       // For now, return parent's world matrix (may be stale, but will be updated next frame)
       return parentTransform->worldMatrix;
   }
   ```

**Action Items:**
- [ ] Create TransformSystem.hpp with class declaration
- [ ] Create TransformSystem.cpp with implementations
- [ ] Verify GLM matrix operations compile correctly
- [ ] Test basic compilation

#### Step 1.2.3: Register System in Application

**File:** `engine/core/app/Application.cpp`

1. **Add system registration:**
   ```cpp
   void Application::registerSystems() {
       // Register TransformSystem (runs early, no dependencies)
       _ecsWorld.registerSystem<TransformSystem>();
       
       // Other systems...
   }
   ```

2. **Call in initialize():**
   ```cpp
   void Application::initialize() {
       // ... existing code ...
       registerTransformComponent();
       registerSystems();
       // ... rest of initialization ...
   }
   ```

**Action Items:**
- [ ] Add `registerSystems()` method
- [ ] Register TransformSystem
- [ ] Verify system appears in execution graph
- [ ] Test that system runs each frame

#### Step 1.2.4: Test Transform Propagation

**Create test:** `tests/core/systems/TransformSystem_test.cpp`

1. **Test basic local matrix computation:**
   ```cpp
   TEST(TransformSystem, LocalMatrixComputation) {
       TransformSystem system;
       Transform2D transform;
       transform.position = {10.0f, 20.0f};
       transform.rotation = 0.0f;
       transform.scale = {1.0f, 1.0f};
       
       glm::mat3 local = system.computeLocalMatrix(transform);
       // Verify translation is correct
       EXPECT_FLOAT_EQ(local[2][0], 10.0f);  // Translation X
       EXPECT_FLOAT_EQ(local[2][1], 20.0f);  // Translation Y
   }
   ```

2. **Test parent-child propagation:**
   ```cpp
   TEST(TransformSystem, ParentChildPropagation) {
       World world;
       TransformSystem system;
       
       // Create parent
       EntityId parent = world.createEntity();
       auto& parentTransform = world.addComponent<Transform2D>(parent);
       parentTransform.position = {100.0f, 200.0f};
       parentTransform.dirty = true;
       
       // Create child
       EntityId child = world.createEntity();
       auto& childTransform = world.addComponent<Transform2D>(child);
       childTransform.position = {10.0f, 20.0f};
       childTransform.parent = parent;
       childTransform.depth = 1;
       childTransform.dirty = true;
       
       // Set parent-child relationship
       parentTransform.children.push_back(child);
       
       // Run system
       system.update(world, 0.0f);
       
       // Verify parent world matrix
       EXPECT_FLOAT_EQ(parentTransform.worldMatrix[2][0], 100.0f);
       EXPECT_FLOAT_EQ(parentTransform.worldMatrix[2][1], 200.0f);
       
       // Verify child world matrix (parent position + child local position)
       EXPECT_FLOAT_EQ(childTransform.worldMatrix[2][0], 110.0f);  // 100 + 10
       EXPECT_FLOAT_EQ(childTransform.worldMatrix[2][1], 220.0f);  // 200 + 20
   }
   ```

**Action Items:**
- [ ] Write transform propagation tests
- [ ] Run tests and verify correctness
- [ ] Test with multiple levels of hierarchy (grandparent → parent → child)
- [ ] Test with rotation and scale

---

## Step 1.3: Hierarchy Manipulation Utilities

### Objective
Provide functions to establish, query, and modify parent-child relationships between entities.

### Design Decision: Where to Place Functions

**Option A:** Add methods directly to `World` class
- Pros: Direct access, no namespace pollution
- Cons: World class becomes larger

**Option B:** Create `SceneGraph` namespace (future-proof for Phase 2)
- Pros: Organized, extensible, matches future SceneGraph utilities
- Cons: Slightly more verbose

**Decision:** Use Option B - Create `SceneGraph` namespace in `engine/core/scene/SceneGraph.hpp` (even though we're in Phase 1, this sets up Phase 2 nicely).

### File Structure
```
engine/core/scene/
├── SceneGraph.hpp
└── SceneGraph.cpp
```

### Detailed Implementation Steps

#### Step 1.3.1: Create SceneGraph Header

**File:** `engine/core/scene/SceneGraph.hpp`

```cpp
#pragma once

#include "../ecs/World.hpp"
#include "../ecs/entity/EntityId.hpp"
#include "../components/Transform2D.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace SceneGraph {
    // ========================================================================
    // Hierarchy Manipulation
    // ========================================================================
    
    /**
     * Sets the parent of an entity.
     * If parent is invalid EntityId, makes entity a root (removes from current parent).
     * 
     * @param world - The ECS world
     * @param child - Entity to reparent
     * @param parent - New parent entity (or invalid EntityId for root)
     * 
     * @throws std::runtime_error if circular dependency detected
     */
    void setParent(World& world, EntityId child, EntityId parent);
    
    /**
     * Removes an entity from its parent, making it a root.
     * Equivalent to setParent(world, entity, invalidEntityId).
     */
    void removeFromParent(World& world, EntityId entity);
    
    /**
     * Gets the parent of an entity.
     * @returns Parent EntityId, or invalid EntityId if root
     */
    EntityId getParent(World& world, EntityId entity);
    
    /**
     * Gets all direct children of an entity.
     * @returns Vector of child EntityIds (empty if none)
     */
    std::vector<EntityId> getChildren(World& world, EntityId entity);
    
    /**
     * Gets all ancestor entities (parent, grandparent, etc.) up to root.
     * @returns Vector of ancestor EntityIds, ordered from parent to root
     */
    std::vector<EntityId> getAncestors(World& world, EntityId entity);
    
    /**
     * Gets all descendant entities (children, grandchildren, etc.).
     * @returns Vector of all descendant EntityIds
     */
    std::vector<EntityId> getDescendants(World& world, EntityId entity);
    
    /**
     * Destroys an entity and all its descendants recursively.
     * Useful for destroying entire subtrees.
     */
    void destroyRecursive(World& world, EntityId entity);
    
    // ========================================================================
    // Transform Utilities
    // ========================================================================
    
    /**
     * Gets the world position of an entity (extracted from world matrix).
     */
    glm::vec2 getWorldPosition(World& world, EntityId entity);
    
    /**
     * Sets the world position of an entity (updates local position relative to parent).
     */
    void setWorldPosition(World& world, EntityId entity, glm::vec2 worldPos);
    
    /**
     * Converts a local position to world coordinates.
     */
    glm::vec2 localToWorld(World& world, EntityId entity, glm::vec2 localPos);
    
    /**
     * Converts a world position to local coordinates.
     */
    glm::vec2 worldToLocal(World& world, EntityId entity, glm::vec2 worldPos);
}
```

#### Step 1.3.2: Implement Hierarchy Functions

**File:** `engine/core/scene/SceneGraph.cpp`

1. **Implement setParent():**
   ```cpp
   #include "SceneGraph.hpp"
   #include <stdexcept>
   #include <algorithm>
   
   void SceneGraph::setParent(World& world, EntityId child, EntityId parent) {
       // Validate entities are alive
       if (!world.isAlive(child)) {
           throw std::runtime_error("Cannot set parent: child entity is not alive");
       }
       
       // Get child's transform (must exist)
       auto* childTransform = world.getComponent<Transform2D>(child);
       if (!childTransform) {
           throw std::runtime_error("Cannot set parent: child entity has no Transform2D component");
       }
       
       // If parent is invalid, make root
       if (!world.isAlive(parent) || parent.index == 0) {
           removeFromParent(world, child);
           return;
       }
       
       // Validate parent has Transform2D
       auto* parentTransform = world.getComponent<Transform2D>(parent);
       if (!parentTransform) {
           throw std::runtime_error("Cannot set parent: parent entity has no Transform2D component");
       }
       
       // Check for circular dependency
       EntityId current = parent;
       while (world.isAlive(current)) {
           if (current == child) {
               throw std::runtime_error("Cannot set parent: circular dependency detected");
           }
           auto* currentTransform = world.getComponent<Transform2D>(current);
           if (!currentTransform || !world.isAlive(currentTransform->parent)) {
               break;
           }
           current = currentTransform->parent;
       }
       
       // Remove child from old parent's children list
       EntityId oldParent = childTransform->parent;
       if (world.isAlive(oldParent)) {
           auto* oldParentTransform = world.getComponent<Transform2D>(oldParent);
           if (oldParentTransform) {
               auto& children = oldParentTransform->children;
               children.erase(
                   std::remove(children.begin(), children.end(), child),
                   children.end()
               );
           }
       }
       
       // Add child to new parent's children list
       parentTransform->children.push_back(child);
       
       // Update child's parent and depth
       childTransform->parent = parent;
       childTransform->depth = parentTransform->depth + 1;
       
       // Recursively update depths of all descendants
       updateDepthsRecursive(world, child, childTransform->depth);
       
       // Mark child and all descendants as dirty
       markDirtyRecursive(world, child);
   }
   ```

2. **Implement helper functions:**
   ```cpp
   namespace {
       // Internal helper to recursively update depths
       void updateDepthsRecursive(World& world, EntityId entity, int32_t newDepth) {
           auto* transform = world.getComponent<Transform2D>(entity);
           if (!transform) return;
           
           transform->depth = newDepth;
           for (EntityId child : transform->children) {
               if (world.isAlive(child)) {
                   updateDepthsRecursive(world, child, newDepth + 1);
               }
           }
       }
       
       // Internal helper to recursively mark dirty
       void markDirtyRecursive(World& world, EntityId entity) {
           auto* transform = world.getComponent<Transform2D>(entity);
           if (!transform) return;
           
           transform->dirty = true;
           for (EntityId child : transform->children) {
               if (world.isAlive(child)) {
                   markDirtyRecursive(world, child);
               }
           }
       }
   }
   ```

3. **Implement removeFromParent():**
   ```cpp
   void SceneGraph::removeFromParent(World& world, EntityId entity) {
       auto* transform = world.getComponent<Transform2D>(entity);
       if (!transform) return;
       
       EntityId parent = transform->parent;
       if (!world.isAlive(parent)) {
           return;  // Already root
       }
       
       // Remove from parent's children list
       auto* parentTransform = world.getComponent<Transform2D>(parent);
       if (parentTransform) {
           auto& children = parentTransform->children;
           children.erase(
               std::remove(children.begin(), children.end(), entity),
               children.end()
           );
       }
       
       // Make root
       transform->parent = EntityId{0, 0};
       transform->depth = 0;
       
       // Update depths of descendants
       updateDepthsRecursive(world, entity, 0);
       
       // Mark dirty
       markDirtyRecursive(world, entity);
   }
   ```

4. **Implement query functions:**
   ```cpp
   EntityId SceneGraph::getParent(World& world, EntityId entity) {
       auto* transform = world.getComponent<Transform2D>(entity);
       if (!transform) return EntityId{0, 0};
       return transform->parent;
   }
   
   std::vector<EntityId> SceneGraph::getChildren(World& world, EntityId entity) {
       auto* transform = world.getComponent<Transform2D>(entity);
       if (!transform) return {};
       return transform->children;  // Return copy
   }
   
   std::vector<EntityId> SceneGraph::getAncestors(World& world, EntityId entity) {
       std::vector<EntityId> ancestors;
       EntityId current = entity;
       
       while (world.isAlive(current)) {
           auto* transform = world.getComponent<Transform2D>(current);
           if (!transform) break;
           
           EntityId parent = transform->parent;
           if (!world.isAlive(parent)) break;
           
           ancestors.push_back(parent);
           current = parent;
       }
       
       return ancestors;
   }
   
   std::vector<EntityId> SceneGraph::getDescendants(World& world, EntityId entity) {
       std::vector<EntityId> descendants;
       
       std::function<void(EntityId)> collect = [&](EntityId e) {
           auto* transform = world.getComponent<Transform2D>(e);
           if (!transform) return;
           
           for (EntityId child : transform->children) {
               if (world.isAlive(child)) {
                   descendants.push_back(child);
                   collect(child);
               }
           }
       };
       
       collect(entity);
       return descendants;
   }
   ```

5. **Implement destroyRecursive():**
   ```cpp
   void SceneGraph::destroyRecursive(World& world, EntityId entity) {
       auto* transform = world.getComponent<Transform2D>(entity);
       if (transform) {
           // Destroy all children first (recursive)
           for (EntityId child : transform->children) {
               if (world.isAlive(child)) {
                   destroyRecursive(world, child);
               }
           }
       }
       
       // Remove from parent before destroying
       removeFromParent(world, entity);
       
       // Destroy entity
       world.destroyEntity(entity);
   }
   ```

6. **Implement transform utilities:**
   ```cpp
   glm::vec2 SceneGraph::getWorldPosition(World& world, EntityId entity) {
       auto* transform = world.getComponent<Transform2D>(entity);
       if (!transform) return {0.0f, 0.0f};
       
       // Extract translation from world matrix
       return {transform->worldMatrix[2][0], transform->worldMatrix[2][1]};
   }
   
   void SceneGraph::setWorldPosition(World& world, EntityId entity, glm::vec2 worldPos) {
       auto* transform = world.getComponent<Transform2D>(entity);
       if (!transform) return;
       
       // Get parent's world matrix
       glm::mat3 parentWorld{1.0f};
       if (world.isAlive(transform->parent)) {
           auto* parentTransform = world.getComponent<Transform2D>(transform->parent);
           if (parentTransform) {
               parentWorld = parentTransform->worldMatrix;
           }
       }
       
       // Compute inverse parent transform
       glm::mat3 invParent = glm::inverse(parentWorld);
       
       // Transform world position to local
       glm::vec3 worldPos3 = {worldPos.x, worldPos.y, 1.0f};
       glm::vec3 localPos3 = invParent * worldPos3;
       
       // Update local position
       transform->position = {localPos3.x, localPos3.y};
       transform->dirty = true;
   }
   
   glm::vec2 SceneGraph::localToWorld(World& world, EntityId entity, glm::vec2 localPos) {
       auto* transform = world.getComponent<Transform2D>(entity);
       if (!transform) return localPos;
       
       glm::vec3 localPos3 = {localPos.x, localPos.y, 1.0f};
       glm::vec3 worldPos3 = transform->worldMatrix * localPos3;
       return {worldPos3.x, worldPos3.y};
   }
   
   glm::vec2 SceneGraph::worldToLocal(World& world, EntityId entity, glm::vec2 worldPos) {
       auto* transform = world.getComponent<Transform2D>(entity);
       if (!transform) return worldPos;
       
       glm::mat3 invWorld = glm::inverse(transform->worldMatrix);
       glm::vec3 worldPos3 = {worldPos.x, worldPos.y, 1.0f};
       glm::vec3 localPos3 = invWorld * worldPos3;
       return {localPos3.x, localPos3.y};
   }
   ```

**Action Items:**
- [ ] Create SceneGraph.hpp with all function declarations
- [ ] Create SceneGraph.cpp with implementations
- [ ] Add to CMakeLists.txt
- [ ] Compile and fix any errors
- [ ] Test each function individually

#### Step 1.3.3: Test Hierarchy Functions

**Create test:** `tests/core/scene/SceneGraph_test.cpp`

Test cases:
1. `setParent()` - Basic functionality
2. `setParent()` - Circular dependency prevention
3. `getChildren()` - Returns correct list
4. `removeFromParent()` - Makes root correctly
5. `getAncestors()` - Returns correct chain
6. `getDescendants()` - Returns all descendants
7. `destroyRecursive()` - Destroys entire subtree
8. Depth updates correctly when reparenting

**Action Items:**
- [ ] Write comprehensive hierarchy tests
- [ ] Run tests and verify all pass
- [ ] Test edge cases (empty children, deep hierarchies)

---

## Step 1.4: Comprehensive Testing

### Objective
Ensure Phase 1 implementation is correct, efficient, and handles edge cases.

### Test Categories

#### 1.4.1: Component Tests

**File:** `tests/core/components/Transform2D_test.cpp`

Test cases:
- [ ] Default constructor sets correct initial values
- [ ] Convenience constructor works
- [ ] Field access and modification
- [ ] JSON deserialization (position, rotation, scale)
- [ ] Invalid JSON handling
- [ ] Component can be added/removed from entities

#### 1.4.2: Transform Math Tests

**File:** `tests/core/systems/TransformSystem_math_test.cpp`

Test cases:
- [ ] Local matrix computation (translation only)
- [ ] Local matrix computation (rotation only)
- [ ] Local matrix computation (scale only)
- [ ] Local matrix computation (all combined)
- [ ] World matrix with identity parent
- [ ] World matrix with translated parent
- [ ] World matrix with rotated parent
- [ ] World matrix with scaled parent
- [ ] Nested transforms (3+ levels)
- [ ] Matrix multiplication order correctness

#### 1.4.3: System Propagation Tests

**File:** `tests/core/systems/TransformSystem_propagation_test.cpp`

Test cases:
- [ ] Single entity transform update
- [ ] Parent-child propagation (2 levels)
- [ ] Grandparent-parent-child (3 levels)
- [ ] Multiple children of same parent
- [ ] Dirty flag propagation to children
- [ ] Depth-based sorting ensures correct update order
- [ ] Only dirty transforms are updated
- [ ] Clean transforms are not recomputed

#### 1.4.4: Hierarchy Operation Tests

**File:** `tests/core/scene/SceneGraph_hierarchy_test.cpp`

Test cases:
- [ ] `setParent()` basic functionality
- [ ] `setParent()` prevents circular dependency
- [ ] `setParent()` updates depth correctly
- [ ] `setParent()` marks dirty correctly
- [ ] `getChildren()` returns correct list
- [ ] `getAncestors()` returns correct chain
- [ ] `getDescendants()` returns all descendants
- [ ] `removeFromParent()` makes root
- [ ] `removeFromParent()` updates depth
- [ ] `destroyRecursive()` destroys entire subtree
- [ ] Reparenting maintains consistency

#### 1.4.5: Integration Tests

**File:** `tests/integration/TransformHierarchy_integration_test.cpp`

Test cases:
- [ ] Create hierarchy → update parent → verify child updates
- [ ] Reparent entity → verify transforms update
- [ ] Destroy parent → verify children become roots
- [ ] Complex hierarchy (10+ levels deep)
- [ ] Multiple independent hierarchies
- [ ] Transform updates across multiple frames

#### 1.4.6: Performance Tests

**File:** `tests/performance/TransformSystem_performance_test.cpp`

Test cases:
- [ ] 1,000 entities with hierarchy updates in <16ms (60 FPS)
- [ ] 10,000 entities with hierarchy updates in <16ms
- [ ] Deep hierarchy (100 levels) update performance
- [ ] Wide hierarchy (1 parent, 1000 children) update performance
- [ ] Mixed hierarchy (balanced tree) update performance
- [ ] Dirty flag optimization effectiveness (only dirty transforms updated)

**Performance Benchmarks:**
```cpp
TEST(TransformSystem, Performance_10000Entities) {
    World world;
    TransformSystem system;
    
    // Create 10,000 entities in hierarchy
    // ... setup code ...
    
    auto start = std::chrono::high_resolution_clock::now();
    system.update(world, 0.0f);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_LT(duration.count(), 16);  // Must complete in <16ms for 60 FPS
}
```

**Action Items:**
- [ ] Write all test categories
- [ ] Run test suite and fix any failures
- [ ] Achieve >90% code coverage
- [ ] Verify performance benchmarks pass
- [ ] Document any performance issues or optimizations needed

---

## Step 1.5: Integration with Existing Systems

### Objective
Ensure Transform2D and TransformSystem work correctly with existing engine systems.

### Integration Points

#### 1.5.1: Renderer Integration (Future)

**Note:** This may be deferred if Renderer2D doesn't exist yet, but document the integration point:

```cpp
// In RenderSystem or similar
void RenderSystem::update(World& world, float dt) {
    // TransformSystem must run first (dependency)
    // Then we can use worldMatrix for rendering
    
    for (auto [entity, sprite, transform] : world.view<SpriteComponent, Transform2D>()) {
        // Use transform->worldMatrix for rendering position
        renderer.drawSprite(sprite, transform->worldMatrix);
    }
}
```

#### 1.5.2: Script Integration (Future)

**Note:** ECS host functions already exist, but we may want to add transform-specific functions:

```cpp
// Potential script API (future)
EntityId child = Entity.create();
EntityId parent = Entity.create();
SceneGraph.setParent(child, parent);
```

#### 1.5.3: SceneLoader Integration

**File:** `engine/core/app/SceneLoader.cpp`

Update scene loading to support hierarchy in JSON:
```cpp
// In createEntityFromJson()
if (entityJson.contains("parent")) {
    std::string parentId = entityJson["parent"].get<std::string>();
    // Resolve parent EntityId from ID map
    // Call SceneGraph::setParent()
}
```

**Action Items:**
- [ ] Document integration points
- [ ] Test with existing systems (if any)
- [ ] Update SceneLoader if needed (or defer to Phase 2)

---

## Step 1.6: Documentation & Code Review

### Objective
Ensure code is well-documented and ready for Phase 2.

### Documentation Tasks

1. **Code Comments:**
   - [ ] Add Doxygen-style comments to all public functions
   - [ ] Document algorithm choices (why depth sorting, why dirty flags)
   - [ ] Explain matrix math operations

2. **API Documentation:**
   - [ ] Document Transform2D component fields
   - [ ] Document TransformSystem behavior
   - [ ] Document SceneGraph namespace functions
   - [ ] Provide usage examples

3. **Architecture Documentation:**
   - [ ] Document transform propagation algorithm
   - [ ] Document dirty flag system
   - [ ] Document depth tracking system

4. **Code Review Checklist:**
   - [ ] All functions have error handling
   - [ ] No memory leaks (use smart pointers where appropriate)
   - [ ] No undefined behavior
   - [ ] Code follows project style guidelines
   - [ ] All tests pass
   - [ ] Performance meets requirements

**Action Items:**
- [ ] Add comprehensive code comments
- [ ] Create API documentation
- [ ] Review code for issues
- [ ] Get peer review (if available)

---

## Phase 1 Completion Checklist

### Functional Requirements
- [ ] Transform2D component created and registered
- [ ] TransformSystem propagates transforms correctly
- [ ] Parent-child relationships can be established
- [ ] Child transforms update when parent changes
- [ ] Hierarchy manipulation functions work
- [ ] All unit tests pass
- [ ] Performance benchmarks pass (10,000 entities @ 60 FPS)

### Code Quality
- [ ] Code compiles without warnings
- [ ] All functions documented
- [ ] Error handling implemented
- [ ] Memory safety verified
- [ ] Code follows project conventions

### Integration
- [ ] Component integrates with World
- [ ] System integrates with SystemScheduler
- [ ] Works with existing ECS systems
- [ ] Ready for Phase 2 integration

---

## Estimated Time Breakdown

- **Step 1.1 (Transform2D Component):** 3-4 hours
- **Step 1.2 (TransformSystem):** 6-8 hours
- **Step 1.3 (Hierarchy Utilities):** 6-8 hours
- **Step 1.4 (Testing):** 8-10 hours
- **Step 1.5 (Integration):** 2-3 hours
- **Step 1.6 (Documentation):** 2-3 hours

**Total Phase 1:** 27-36 hours

---

## Common Pitfalls & Solutions

### Pitfall 1: Matrix Multiplication Order
**Issue:** Wrong order (local * parent instead of parent * local)
**Solution:** Always use `parentWorld * localMatrix` (parent on left)

### Pitfall 2: Circular Dependencies
**Issue:** setParent() allows child to be parent of its own ancestor
**Solution:** Traverse up parent chain and check for cycle before setting

### Pitfall 3: Stale World Matrices
**Issue:** Child uses parent's world matrix before parent updates
**Solution:** Sort by depth, always update parents before children

### Pitfall 4: Depth Not Updated on Reparent
**Issue:** Reparenting doesn't update depth of descendants
**Solution:** Recursively update depths after reparenting

### Pitfall 5: Dirty Flag Not Propagated
**Issue:** Parent changes but children don't recompute
**Solution:** Mark all children dirty when parent updates

---

## Next Steps After Phase 1

Once Phase 1 is complete:
1. ✅ Transform hierarchy foundation is solid
2. ✅ Ready to build Scene class (Phase 2)
3. ✅ Ready to add scene management (Phase 2)
4. ✅ Ready for serialization (Phase 5)

**Phase 1 is the critical foundation - take time to get it right!**
