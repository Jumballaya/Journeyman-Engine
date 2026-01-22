# Phase 6: Integration & Polish - Comprehensive Implementation Breakdown

## Overview

Phase 6 is the final integration and polish phase for the Scene Graph system. This phase focuses on:
1. **Application Integration** - Replace SceneLoader with SceneManager, integrate SaveManager
2. **World Scene Tracking** - Add entity-to-scene mapping for scene-aware queries
3. **Renderer Integration** - Ensure renderer works seamlessly with scene system
4. **Comprehensive Testing** - Unit, integration, and performance tests
5. **Documentation** - Complete API documentation and usage examples
6. **Demo Scene** - Create a comprehensive demo showcasing all features

**Goal:** Integrate all scene graph features into the application, ensure everything works together, and polish the system for production use.

**Success Criteria:**
- ✅ SceneLoader replaced with SceneManager in Application
- ✅ SaveManager integrated and functional
- ✅ Entities can be tracked by scene
- ✅ Renderer works with scene system
- ✅ All tests pass (unit, integration, performance)
- ✅ Complete documentation exists
- ✅ Demo scene demonstrates all features

**Prerequisites:**
- ✅ Phase 1 complete (Transform Hierarchy)
- ✅ Phase 2 complete (Scene Structure)
- ✅ Phase 3 complete (Scene Manager Features)
- ✅ Phase 4 complete (Scene Transitions)
- ✅ Phase 5 complete (Serialization & Save System)

---

## Step 6.1: Application Integration

### Objective
Replace the old SceneLoader with SceneManager, integrate SaveManager, and update Application to use the new scene system.

### Design Concept

**Current State:**
- Application uses `SceneLoader` to load scenes
- No SaveManager
- No SceneManager

**New State:**
- Application uses `SceneManager` for all scene operations
- SaveManager integrated for save/load functionality
- Application provides accessors for SceneManager and SaveManager

### Detailed Implementation Steps

#### Step 6.1.1: Update Application Header

**File:** `engine/core/app/Application.hpp`

Replace SceneLoader with SceneManager and add SaveManager:
```cpp
#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>

#include "../assets/AssetManager.hpp"
#include "../ecs/World.hpp"
#include "../events/EventBus.hpp"
#include "../scripting/ScriptManager.hpp"
#include "../tasks/JobSystem.hpp"
#include "GameManifest.hpp"
#include "ModuleRegistry.hpp"
// Remove: #include "SceneLoader.hpp"
#include "../scene/SceneManager.hpp"  // NEW
#include "../save/SaveManager.hpp"   // NEW

class Application {
public:
    Application(const std::filesystem::path& rootDir, const std::filesystem::path& manifestPath);
    ~Application();

    void initialize();
    void run();
    void shutdown();

    World& getWorld();
    JobSystem& getJobSystem();
    AssetManager& getAssetManager();
    ScriptManager& getScriptManager();
    const GameManifest& getManifest() const;

    EventBus& getEventBus() { return _eventBus; }
    const EventBus& getEventBus() const { return _eventBus; }
    
    // NEW: Scene management accessors
    SceneManager& getSceneManager() { return _sceneManager; }
    const SceneManager& getSceneManager() const { return _sceneManager; }
    
    // NEW: Save management accessor
    SaveManager& getSaveManager() { return _saveManager; }
    const SaveManager& getSaveManager() const { return _saveManager; }

private:
    using Clock = std::chrono::high_resolution_clock;
    Clock::time_point _previousFrameTime;
    float _maxDeltaTime = 0.33f;  // clamping to 30 FPS at max
    bool _running = false;

    std::filesystem::path _manifestPath;
    std::filesystem::path _rootDir;
    GameManifest _manifest;

    World _ecsWorld;
    JobSystem _jobSystem;
    AssetManager _assetManager;
    // Remove: SceneLoader _sceneLoader;
    SceneManager _sceneManager;  // NEW
    SaveManager _saveManager;    // NEW
    ScriptManager _scriptManager;
    EventBus _eventBus{8192};

    void loadAndParseManifest();
    void registerScriptModule();
    void initializeGameFiles();
    void loadScenes();  // Updated to use SceneManager
};
```

**Action Items:**
- [ ] Remove SceneLoader include
- [ ] Add SceneManager and SaveManager includes
- [ ] Replace SceneLoader member with SceneManager
- [ ] Add SaveManager member
- [ ] Add accessor methods
- [ ] Verify compilation

#### Step 6.1.2: Update Application Constructor

**File:** `engine/core/app/Application.cpp`

Update constructor to initialize SceneManager and SaveManager:
```cpp
Application::Application(const std::filesystem::path& rootDir, const std::filesystem::path& manifestPath)
    : _manifestPath(manifestPath), 
      _rootDir(rootDir), 
      _assetManager(_rootDir), 
      // Remove: _sceneLoader(_ecsWorld, _assetManager),
      _sceneManager(_ecsWorld, _assetManager, _eventBus),  // NEW
      _saveManager(_sceneManager, _ecsWorld),              // NEW
      _scriptManager() {
}
```

**Action Items:**
- [ ] Update constructor initialization list
- [ ] Initialize SceneManager with World, AssetManager, EventBus
- [ ] Initialize SaveManager with SceneManager, World
- [ ] Verify compilation

#### Step 6.1.3: Update loadScenes() Method

**File:** `engine/core/app/Application.cpp`

Replace SceneLoader usage with SceneManager:
```cpp
void Application::loadScenes() {
    // Old code:
    // JM_LOG_INFO("[Scene Loading]: {}", _manifest.entryScene);
    // _sceneLoader.loadScene(_manifest.entryScene);
    // JM_LOG_INFO("[Scene Loaded]: {}", _sceneLoader.getCurrentSceneName());
    
    // New code:
    JM_LOG_INFO("[Scene Loading]: {}", _manifest.entryScene);
    SceneHandle handle = _sceneManager.loadScene(_manifest.entryScene);
    if (handle.isValid()) {
        Scene* scene = _sceneManager.getScene(handle);
        if (scene) {
            JM_LOG_INFO("[Scene Loaded]: {}", scene->getName());
        } else {
            JM_LOG_ERROR("[Scene Loading Failed]: Could not get scene from handle");
        }
    } else {
        JM_LOG_ERROR("[Scene Loading Failed]: Invalid scene handle returned");
    }
}
```

**Action Items:**
- [ ] Replace SceneLoader calls with SceneManager
- [ ] Update logging to use SceneManager
- [ ] Add error handling for failed loads
- [ ] Test scene loading works

#### Step 6.1.4: Update Application::run() Method

**File:** `engine/core/app/Application.cpp`

Add SceneManager and SaveManager updates to the main loop:
```cpp
void Application::run() {
    _running = true;
    _previousFrameTime = Clock::now();

    while (_running) {
        auto currentTime = Clock::now();
        std::chrono::duration<float> deltaTime = currentTime - _previousFrameTime;
        _previousFrameTime = currentTime;

        float dt = deltaTime.count();
        if (dt > _maxDeltaTime) {
            dt = _maxDeltaTime;
        }

        _jobSystem.beginFrame();

        TaskGraph graph;
        _ecsWorld.buildExecutionGraph(graph, dt);
        GetModuleRegistry().buildAsyncTicks(graph, dt);
        _jobSystem.execute(graph);

        _jobSystem.endFrame();

        _eventBus.dispatch();

        // NEW: Update scene manager
        _sceneManager.update(dt);
        
        // NEW: Update save manager (handles auto-save)
        _saveManager.update(dt);

        GetModuleRegistry().tickMainThreadModules(*this, dt);
    }

    shutdown();
}
```

**Action Items:**
- [ ] Add _sceneManager.update(dt) call
- [ ] Add _saveManager.update(dt) call
- [ ] Verify updates are called every frame
- [ ] Test that scenes update correctly

#### Step 6.1.5: Test Application Integration

**Create test file:** `tests/core/app/ApplicationIntegration_test.cpp`

Test cases:
1. Application initializes with SceneManager
2. Application initializes with SaveManager
3. SceneManager accessor works
4. SaveManager accessor works
5. Scene loading works through Application
6. Scene updates are called every frame
7. SaveManager auto-save works

**Action Items:**
- [ ] Write Application integration tests
- [ ] Run tests and verify all pass
- [ ] Test scene loading from manifest
- [ ] Test save/load through Application

---

## Step 6.2: World Scene Tracking

### Objective
Add entity-to-scene mapping to World, enabling scene-aware entity queries and management.

### Design Concept

**Problem:** Currently, World doesn't know which scene an entity belongs to. This makes it difficult to:
- Query entities by scene
- Unload all entities in a scene
- Track entity ownership

**Solution:** Add a mapping from EntityId to SceneHandle in World, maintained by SceneManager.

### Detailed Implementation Steps

#### Step 6.2.1: Add Scene Tracking to World Header

**File:** `engine/core/ecs/World.hpp`

Add scene tracking methods and data:
```cpp
#pragma once

// ... existing includes ...
#include "../scene/SceneHandle.hpp"  // NEW

class World {
public:
    // ... existing methods ...
    
    // NEW: Scene tracking API
    /**
     * Sets which scene an entity belongs to.
     * Called by SceneManager when entities are created.
     * 
     * @param id - Entity ID
     * @param scene - Scene handle (or invalid for no scene)
     */
    void setEntityScene(EntityId id, SceneHandle scene);
    
    /**
     * Gets which scene an entity belongs to.
     * 
     * @param id - Entity ID
     * @returns Scene handle, or invalid if entity has no scene
     */
    SceneHandle getEntityScene(EntityId id) const;
    
    /**
     * Gets all entities in a specific scene.
     * 
     * @param scene - Scene handle
     * @returns Vector of entity IDs in the scene
     */
    std::vector<EntityId> getEntitiesInScene(SceneHandle scene) const;
    
    /**
     * Removes scene tracking for an entity.
     * Called when entity is destroyed.
     * 
     * @param id - Entity ID
     */
    void clearEntityScene(EntityId id);

private:
    // ... existing members ...
    
    // NEW: Entity-to-scene mapping
    std::unordered_map<EntityId, SceneHandle> _entityScenes;
};
```

**Action Items:**
- [ ] Add SceneHandle include
- [ ] Add scene tracking methods
- [ ] Add _entityScenes member
- [ ] Verify compilation

#### Step 6.2.2: Implement Scene Tracking Methods

**File:** `engine/core/ecs/World.cpp`

Implement scene tracking:
```cpp
#include "World.hpp"
// ... existing includes ...
#include "../scene/SceneHandle.hpp"  // NEW

// ... existing implementations ...

void World::setEntityScene(EntityId id, SceneHandle scene) {
    if (!isAlive(id)) {
        return;  // Entity doesn't exist
    }
    
    _entityScenes[id] = scene;
}

SceneHandle World::getEntityScene(EntityId id) const {
    if (!isAlive(id)) {
        return SceneHandle{};  // Invalid handle
    }
    
    auto it = _entityScenes.find(id);
    if (it == _entityScenes.end()) {
        return SceneHandle{};  // No scene assigned
    }
    
    return it->second;
}

std::vector<EntityId> World::getEntitiesInScene(SceneHandle scene) const {
    std::vector<EntityId> entities;
    
    for (const auto& [entityId, sceneHandle] : _entityScenes) {
        if (sceneHandle == scene && isAlive(entityId)) {
            entities.push_back(entityId);
        }
    }
    
    return entities;
}

void World::clearEntityScene(EntityId id) {
    _entityScenes.erase(id);
}
```

**Action Items:**
- [ ] Implement setEntityScene()
- [ ] Implement getEntityScene()
- [ ] Implement getEntitiesInScene()
- [ ] Implement clearEntityScene()
- [ ] Verify compilation

#### Step 6.2.3: Update destroyEntity() to Clear Scene Tracking

**File:** `engine/core/ecs/World.cpp`

Update destroyEntity() to clean up scene tracking:
```cpp
void World::destroyEntity(EntityId id) {
    if (!isAlive(id)) {
        return;
    }
    
    // Clear scene tracking
    clearEntityScene(id);  // NEW
    
    // Clear tags
    clearTags(id);
    
    // ... rest of destroy logic ...
}
```

**Action Items:**
- [ ] Add clearEntityScene() call to destroyEntity()
- [ ] Verify entities are cleaned up correctly
- [ ] Test scene tracking cleanup

#### Step 6.2.4: Update SceneManager to Set Entity Scenes

**File:** `engine/core/scene/SceneManager.cpp`

Update Scene::createEntity() to set entity scene:
```cpp
EntityId Scene::createEntity(EntityId parent) {
    EntityId entity = parent.isValid() 
        ? SceneGraph::createChild(_world, parent)
        : SceneGraph::createChild(_world, _root);
    
    // NEW: Set entity scene
    _world.setEntityScene(entity, _handle);
    
    return entity;
}
```

**Note:** This assumes Scene has access to its handle. If not, SceneManager should set the scene after creation.

**Alternative:** SceneManager sets scene after entity creation:
```cpp
// In SceneManager::loadScene()
EntityId entity = scene->createEntity(parent);
_world.setEntityScene(entity, handle);
```

**Action Items:**
- [ ] Update Scene::createEntity() to set scene
- [ ] Or update SceneManager to set scene after creation
- [ ] Test entities are tracked correctly
- [ ] Verify scene queries work

#### Step 6.2.5: Test World Scene Tracking

**Create test file:** `tests/core/ecs/WorldSceneTracking_test.cpp`

Test cases:
1. setEntityScene() tracks entity correctly
2. getEntityScene() returns correct scene
3. getEntitiesInScene() returns all entities in scene
4. clearEntityScene() removes tracking
5. destroyEntity() clears scene tracking
6. SceneManager sets entity scenes correctly
7. Query entities by scene works

**Action Items:**
- [ ] Write World scene tracking tests
- [ ] Run tests and verify all pass
- [ ] Test with multiple scenes
- [ ] Test entity cleanup

---

## Step 6.3: Renderer Integration

### Objective
Ensure Renderer2D works seamlessly with the scene system, supporting scene-based rendering and transitions.

### Design Concept

**Current State:**
- Renderer2D renders all sprites to _sceneSurface
- No scene awareness
- No transition support

**New State:**
- Renderer2D can render specific scenes
- Supports transition rendering (from Phase 4)
- Scene-aware rendering

### Detailed Implementation Steps

#### Step 6.3.1: Verify Renderer2D Scene Support

**File:** `engine/renderer2d/Renderer2D.hpp`

Check if Phase 4 transition methods exist:
- `beginScene(int bufferIndex)`
- `endScene()`
- `renderTransition(SceneTransition&, Scene*, Scene*)`
- `getSceneBufferTexture(int index)`

If not, refer to Phase 4 breakdown for implementation.

**Action Items:**
- [ ] Check if Phase 4 renderer methods exist
- [ ] If missing, implement them (see Phase 4 breakdown)
- [ ] Verify renderer supports scene buffers

#### Step 6.3.2: Add Scene-Aware Rendering Helper

**File:** `engine/renderer2d/Renderer2D.hpp`

Add helper to render entities from a specific scene:
```cpp
class Renderer2D {
public:
    // ... existing methods ...
    
    /**
     * Renders all sprites from entities in a specific scene.
     * 
     * @param world - ECS World
     * @param sceneHandle - Scene to render
     */
    void renderScene(World& world, SceneHandle sceneHandle);
    
    // ... rest of class ...
};
```

**File:** `engine/renderer2d/Renderer2D.cpp`

Implement renderScene():
```cpp
void Renderer2D::renderScene(World& world, SceneHandle sceneHandle) {
    // Get all entities in scene
    std::vector<EntityId> entities = world.getEntitiesInScene(sceneHandle);
    
    // Render sprites from entities
    // This assumes you have a SpriteComponent and Transform2D
    // Adjust based on your actual component structure
    
    auto spriteView = world.view<SpriteComponent, Transform2D>();
    for (EntityId entity : entities) {
        if (!world.isAlive(entity)) continue;
        
        auto* sprite = world.getComponent<SpriteComponent>(entity);
        auto* transform = world.getComponent<Transform2D>(entity);
        
        if (sprite && transform) {
            // Get world transform matrix
            glm::mat4 worldMatrix = transform->getWorldMatrix();
            
            // Render sprite
            drawSprite(
                worldMatrix,
                sprite->color,
                sprite->texRect,
                sprite->layer,
                sprite->texture
            );
        }
    }
}
```

**Note:** This is a simplified version. Adjust based on your actual rendering system and component structure.

**Action Items:**
- [ ] Add renderScene() method
- [ ] Implement scene entity rendering
- [ ] Test rendering works for specific scenes
- [ ] Verify sprites render correctly

#### Step 6.3.3: Integrate Scene Rendering into Application

**File:** `engine/core/app/Application.cpp`

Add rendering call in run() loop (if not already present):
```cpp
void Application::run() {
    // ... existing code ...
    
    while (_running) {
        // ... update code ...
        
        // NEW: Render active scenes
        Scene* activeScene = _sceneManager.getActiveScene();
        if (activeScene) {
            // Get renderer (from module registry or direct access)
            // Renderer2D* renderer = GetModuleRegistry().getRenderer();
            // if (renderer) {
            //     renderer->beginFrame();
            //     renderer->renderScene(_ecsWorld, activeScene->getHandle());
            //     renderer->endFrame();
            // }
        }
        
        // ... rest of loop ...
    }
}
```

**Note:** This depends on how Renderer2D is accessed. Adjust based on your module system.

**Action Items:**
- [ ] Add scene rendering to Application::run()
- [ ] Test scenes render correctly
- [ ] Verify transitions render (if active)
- [ ] Test multiple scenes rendering

#### Step 6.3.4: Test Renderer Integration

**Create test file:** `tests/renderer2d/SceneRendering_test.cpp`

Test cases:
1. renderScene() renders entities in scene
2. Entities not in scene are not rendered
3. Multiple scenes can be rendered separately
4. Transitions render correctly (if Phase 4 complete)
5. Performance: Rendering 10,000 entities

**Action Items:**
- [ ] Write renderer integration tests
- [ ] Run tests and verify all pass
- [ ] Test visual rendering (manual verification)
- [ ] Performance test large scenes

---

## Step 6.4: Comprehensive Testing

### Objective
Create comprehensive test suite covering unit tests, integration tests, and performance tests.

### Detailed Implementation Steps

#### Step 6.4.1: Unit Tests

**Test Coverage Areas:**

1. **Transform System Tests** (from Phase 1)
   - Transform math (local, world, composition)
   - Hierarchy operations (reparent, destroy recursive)
   - Dirty flag propagation

2. **Scene System Tests** (from Phase 2)
   - Scene creation/destruction
   - Entity management in scenes
   - Scene hierarchy operations

3. **SceneManager Tests** (from Phase 3)
   - Scene state machine transitions
   - Scene stack operations
   - Additive scene loading

4. **Scene Transitions Tests** (from Phase 4)
   - Transition lifecycle
   - Fade/Slide/CrossFade transitions
   - Transition rendering

5. **Serialization Tests** (from Phase 5)
   - Component serialization round-trips
   - Scene serialization round-trips
   - Save/load operations

6. **World Scene Tracking Tests** (from Step 6.2)
   - Entity scene assignment
   - Scene queries
   - Cleanup on destroy

**Action Items:**
- [ ] Review all unit tests from previous phases
- [ ] Ensure all tests pass
- [ ] Add missing unit tests
- [ ] Test edge cases
- [ ] Test error handling

#### Step 6.4.2: Integration Tests

**Test Coverage Areas:**

1. **End-to-End Scene Management**
   ```cpp
   TEST(Integration, FullSceneLifecycle) {
       // Setup
       World world;
       AssetManager assets("test_assets");
       EventBus events;
       SceneManager scenes(world, assets, events);
       
       // Load scene
       SceneHandle handle = scenes.loadScene("test_scene.json");
       ASSERT_TRUE(handle.isValid());
       
       // Modify scene
       Scene* scene = scenes.getScene(handle);
       EntityId entity = scene->createEntity();
       // ... modify entity ...
       
       // Unload scene
       scenes.unloadScene(handle);
       
       // Verify cleanup
       ASSERT_FALSE(world.isAlive(entity));
   }
   ```

2. **Scene Transitions with State Preservation**
   ```cpp
   TEST(Integration, SceneTransitionPreservesState) {
       // Load scene A
       // Modify entities
       // Switch to scene B with transition
       // Verify scene A entities preserved during transition
       // Verify scene B loads correctly
   }
   ```

3. **Save/Load with Scene Transitions**
   ```cpp
   TEST(Integration, SaveLoadWithScenes) {
       // Load scene
       // Modify state
       // Save game
       // Modify state
       // Load game
       // Verify state restored
   }
   ```

4. **Multi-Scene Additive Loading**
   ```cpp
   TEST(Integration, AdditiveSceneLoading) {
       // Load base scene
       // Load additive scene
       // Verify both scenes active
       // Verify entities from both scenes exist
       // Unload additive scene
       // Verify base scene still active
   }
   ```

5. **Memory Leak Tests**
   ```cpp
   TEST(Integration, NoMemoryLeaks) {
       // Load/unload scenes repeatedly
       // Create/destroy entities repeatedly
       // Verify no memory leaks
   }
   ```

**Action Items:**
- [ ] Write end-to-end scene lifecycle test
- [ ] Write scene transition state preservation test
- [ ] Write save/load integration test
- [ ] Write additive loading test
- [ ] Write memory leak test
- [ ] Run all integration tests
- [ ] Fix any failures

#### Step 6.4.3: Performance Tests

**Test Coverage Areas:**

1. **Transform System Performance**
   ```cpp
   TEST(Performance, TransformUpdate_10000Entities) {
       // Create 10,000 entities with hierarchy
       // Update transforms
       // Measure time
       // Should be < 16ms for 60 FPS
   }
   ```

2. **Deep Hierarchy Performance**
   ```cpp
   TEST(Performance, DeepHierarchy_100Levels) {
       // Create 100-level deep hierarchy
       // Update root transform
       // Measure propagation time
   }
   ```

3. **Rapid Scene Switching**
   ```cpp
   TEST(Performance, RapidSceneSwitching) {
       // Switch scenes rapidly
       // Measure switch time
       // Verify no crashes
   }
   ```

4. **Large Save File Serialization**
   ```cpp
   TEST(Performance, LargeSaveSerialization) {
       // Create scene with 10,000 entities
       // Serialize scene
       // Measure serialization time
   }
   ```

5. **Rendering Performance**
   ```cpp
   TEST(Performance, Rendering_10000Entities) {
       // Render 10,000 sprites
       // Measure frame time
       // Should maintain 60 FPS
   }
   ```

**Action Items:**
- [ ] Write transform system performance test
- [ ] Write deep hierarchy test
- [ ] Write rapid scene switching test
- [ ] Write large save serialization test
- [ ] Write rendering performance test
- [ ] Run performance tests
- [ ] Profile and optimize hotspots

#### Step 6.4.4: Test Infrastructure

**Ensure test framework is set up:**

- [ ] Google Test (or similar) configured
- [ ] Test files organized in `tests/` directory
- [ ] CMakeLists.txt includes test targets
- [ ] Tests can be run with `ctest` or `make test`
- [ ] CI/CD runs tests automatically

**Action Items:**
- [ ] Verify test framework setup
- [ ] Organize test files
- [ ] Update CMakeLists.txt for tests
- [ ] Create test runner script
- [ ] Document how to run tests

---

## Step 6.5: Documentation

### Objective
Create comprehensive documentation for the scene graph system, including API documentation, usage examples, and guides.

### Detailed Implementation Steps

#### Step 6.5.1: API Documentation

**Create file:** `docs/SCENE_GRAPH_API.md`

Document all public APIs:
- SceneManager API
- Scene API
- SceneGraph utilities
- SceneTransition API
- SaveManager API
- World scene tracking API

**Format:**
```markdown
# Scene Graph API Documentation

## SceneManager

### loadScene(path)
Loads a scene from a file path.

**Parameters:**
- `path` (std::filesystem::path): Path to scene JSON file

**Returns:**
- `SceneHandle`: Handle to loaded scene, or invalid if load failed

**Example:**
```cpp
SceneHandle handle = sceneManager.loadScene("scenes/level1.scene.json");
if (handle.isValid()) {
    Scene* scene = sceneManager.getScene(handle);
    // Use scene...
}
```

// ... more API docs ...
```

**Action Items:**
- [ ] Document SceneManager API
- [ ] Document Scene API
- [ ] Document SceneGraph utilities
- [ ] Document SceneTransition API
- [ ] Document SaveManager API
- [ ] Document World scene tracking API
- [ ] Add code examples for each API

#### Step 6.5.2: Usage Guide

**Create file:** `docs/SCENE_GRAPH_USAGE.md`

Create comprehensive usage guide:
- Loading and unloading scenes
- Creating entities in scenes
- Using scene hierarchy
- Scene transitions
- Save/load system
- Best practices

**Action Items:**
- [ ] Write scene loading guide
- [ ] Write entity management guide
- [ ] Write hierarchy guide
- [ ] Write transitions guide
- [ ] Write save/load guide
- [ ] Add best practices section

#### Step 6.5.3: Architecture Documentation

**Create file:** `docs/SCENE_GRAPH_ARCHITECTURE.md`

Document system architecture:
- Component overview
- Data flow
- System interactions
- Design decisions
- Future considerations

**Action Items:**
- [ ] Document system architecture
- [ ] Create architecture diagrams
- [ ] Document design decisions
- [ ] Document future plans

#### Step 6.5.4: Update README

**File:** `README.md`

Update README with scene graph features:
- Scene system overview
- Quick start guide
- Feature list
- Links to detailed docs

**Action Items:**
- [ ] Update README with scene features
- [ ] Add quick start example
- [ ] Link to detailed documentation
- [ ] Update feature list

---

## Step 6.6: Demo Scene

### Objective
Create a comprehensive demo scene that showcases all scene graph features.

### Detailed Implementation Steps

#### Step 6.6.1: Plan Demo Scene

**Demo Scene Features:**
- Hierarchical entity structure (parent-child relationships)
- Multiple scenes (main game, pause menu, settings)
- Scene transitions (fade, slide, crossfade)
- Save/load functionality
- Entity manipulation (create, destroy, modify)
- Transform hierarchy demonstration

**Action Items:**
- [ ] Plan demo scene structure
- [ ] List all features to demonstrate
- [ ] Create scene JSON files
- [ ] Create scripts for demo

#### Step 6.6.2: Create Demo Scenes

**Create files:**
- `demo_game/scenes/demo_main.scene.json` - Main demo scene
- `demo_game/scenes/demo_menu.scene.json` - Menu overlay scene
- `demo_game/scenes/demo_settings.scene.json` - Settings scene

**Main Demo Scene:**
```json
{
  "name": "Demo Main",
  "version": "1.0",
  "settings": {
    "backgroundColor": [0.1, 0.1, 0.15, 1.0]
  },
  "entities": [
    {
      "id": "player",
      "name": "Player",
      "parent": null,
      "components": {
        "Transform2D": {
          "position": [400, 300],
          "rotation": 0,
          "scale": [1, 1]
        },
        "SpriteComponent": {
          "texture": "assets/textures/player.png",
          "layer": 10
        }
      }
    },
    {
      "id": "player_weapon",
      "name": "Weapon",
      "parent": "player",
      "components": {
        "Transform2D": {
          "position": [20, 0],
          "rotation": 0,
          "scale": [0.5, 0.5]
        },
        "SpriteComponent": {
          "texture": "assets/textures/weapon.png",
          "layer": 11
        }
      }
    }
  ]
}
```

**Action Items:**
- [ ] Create main demo scene JSON
- [ ] Create menu scene JSON
- [ ] Create settings scene JSON
- [ ] Add hierarchical entities
- [ ] Add components to entities

#### Step 6.6.3: Create Demo Scripts

**Create file:** `demo_game/assets/scripts/demo_controller.ts`

Script to demonstrate features:
```typescript
// Demo controller script
// Demonstrates scene graph features

export function update(dt: f32): void {
    // Get player entity
    const player = Entity.findWithTag("player")[0];
    if (!player) return;
    
    // Move player (demonstrates transform updates)
    const transform = Component.getHandle<Transform2D>(player);
    if (transform) {
        // Move logic...
    }
    
    // Check for scene switch input
    if (/* input check */) {
        // Switch scenes (demonstrates transitions)
    }
    
    // Check for save input
    if (/* input check */) {
        // Save game (demonstrates save system)
    }
}
```

**Action Items:**
- [ ] Create demo controller script
- [ ] Add scene switching logic
- [ ] Add save/load logic
- [ ] Add entity manipulation logic
- [ ] Test script works

#### Step 6.6.4: Document Demo Scene

**Create file:** `demo_game/DEMO_SCENE_README.md`

Document demo scene:
- What it demonstrates
- How to run it
- Controls
- Features shown

**Action Items:**
- [ ] Write demo scene documentation
- [ ] Document controls
- [ ] Document features
- [ ] Add screenshots/videos (if possible)

---

## Phase 6 Completion Checklist

### Functional Requirements
- [ ] SceneLoader replaced with SceneManager
- [ ] SaveManager integrated into Application
- [ ] World tracks entities by scene
- [ ] Renderer works with scene system
- [ ] All unit tests pass
- [ ] All integration tests pass
- [ ] All performance tests pass
- [ ] Complete API documentation
- [ ] Usage guide created
- [ ] Demo scene created and documented

### Code Quality
- [ ] Code compiles without warnings
- [ ] All functions documented
- [ ] Error handling implemented
- [ ] Memory safety verified
- [ ] Code follows project conventions
- [ ] No memory leaks

### Integration
- [ ] Application uses SceneManager
- [ ] Application uses SaveManager
- [ ] World scene tracking works
- [ ] Renderer integrates with scenes
- [ ] All systems work together

### Testing
- [ ] Unit test coverage > 80%
- [ ] Integration tests cover main workflows
- [ ] Performance tests meet targets
- [ ] All tests pass
- [ ] Edge cases tested

### Documentation
- [ ] API documentation complete
- [ ] Usage guide complete
- [ ] Architecture documented
- [ ] README updated
- [ ] Demo scene documented

---

## Estimated Time Breakdown

- **Step 6.1 (Application Integration):** 4-6 hours
- **Step 6.2 (World Scene Tracking):** 4-5 hours
- **Step 6.3 (Renderer Integration):** 3-4 hours
- **Step 6.4 (Comprehensive Testing):** 12-16 hours
- **Step 6.5 (Documentation):** 8-10 hours
- **Step 6.6 (Demo Scene):** 6-8 hours

**Total Phase 6:** 37-49 hours

---

## Common Pitfalls & Solutions

### Pitfall 1: SceneLoader Not Fully Replaced
**Issue:** Old SceneLoader code still exists, causing confusion
**Solution:** Completely remove SceneLoader, update all references

### Pitfall 2: Entity Scene Tracking Not Maintained
**Issue:** Entities created outside SceneManager aren't tracked
**Solution:** Ensure all entity creation goes through SceneManager, or add tracking hooks

### Pitfall 3: Renderer Doesn't Support Scenes
**Issue:** Renderer renders all entities, not scene-specific
**Solution:** Add scene filtering to rendering, use World::getEntitiesInScene()

### Pitfall 4: Tests Not Comprehensive
**Issue:** Missing edge cases, integration tests incomplete
**Solution:** Review test coverage, add missing tests, test error cases

### Pitfall 5: Documentation Outdated
**Issue:** Documentation doesn't match implementation
**Solution:** Keep documentation in sync with code, review regularly

---

## Dependencies on Previous Phases

Phase 6 requires:
- ✅ Phase 1: Transform Hierarchy (for entity hierarchy)
- ✅ Phase 2: Scene Structure (Scene, SceneManager basic)
- ✅ Phase 3: Scene Manager Features (full SceneManager)
- ✅ Phase 4: Scene Transitions (transition rendering)
- ✅ Phase 5: Serialization & Save System (SaveManager)

If any previous phase is incomplete, Phase 6 cannot proceed fully.

---

## Success Criteria

Phase 6 is complete when:
- ✅ Application fully uses SceneManager (no SceneLoader)
- ✅ SaveManager integrated and functional
- ✅ World tracks entities by scene
- ✅ Renderer works with scene system
- ✅ All tests pass (unit, integration, performance)
- ✅ Complete documentation exists
- ✅ Demo scene demonstrates all features
- ✅ System is production-ready

---

## Next Steps After Phase 6

Once Phase 6 is complete:
1. ✅ Scene Graph system is complete and production-ready
2. ✅ All features integrated and tested
3. ✅ Documentation complete
4. ✅ Ready for game development
5. ✅ Future enhancements can be added (prefabs, async loading, etc.)

**Phase 6 is the final polish phase - ensure everything works together seamlessly!**
