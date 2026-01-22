# Scene Graph Implementation - Work Breakdown

This document breaks down the Scene Graph Plan into actionable work items organized by sprint/phase.

---

## Sprint 1: Transform Hierarchy (Foundation)

### 1.1 Transform2D Component
- [ ] Create `engine/core/components/Transform2D.hpp`
- [ ] Create `engine/core/components/Transform2D.cpp`
- [ ] Implement Transform2D component struct with:
  - Local transform fields (position, rotation, scale)
  - Cached world transform matrices (localMatrix, worldMatrix)
  - Hierarchy fields (parent, children vector)
  - Dirty tracking (dirty flag, depth field)
- [ ] Register Transform2D component in World with JSON deserializer
- [ ] Test component creation and basic access

### 1.2 TransformSystem
- [ ] Create `engine/core/systems/TransformSystem.hpp`
- [ ] Create `engine/core/systems/TransformSystem.cpp`
- [ ] Implement TransformSystem class extending System
- [ ] Implement system traits (Provides, Writes, Reads, DependsOn)
- [ ] Implement `update()` method
- [ ] Implement `propagateTransforms()` method:
  - Collect all dirty transforms
  - Sort by depth (topological sort)
  - Update local matrices from position/rotation/scale
  - Multiply by parent's world matrix
  - Mark children as dirty
  - Clear dirty flags
- [ ] Implement `updateWorldMatrix()` helper method
- [ ] Register TransformSystem in Application
- [ ] Test transform propagation with simple parent-child relationships

### 1.3 Hierarchy Manipulation Utilities
- [ ] Add `setParent()` method to World (or SceneGraph namespace)
- [ ] Add `getChildren()` method to retrieve child entities
- [ ] Add `removeFromParent()` method
- [ ] Update depth tracking when reparenting
- [ ] Test hierarchy manipulation operations

### 1.4 Unit Tests
- [ ] Write unit tests for Transform2D component
- [ ] Write unit tests for transform matrix math (local, world, composition)
- [ ] Write unit tests for hierarchy operations (setParent, getChildren, removeFromParent)
- [ ] Write unit tests for TransformSystem dirty propagation
- [ ] Write performance test for 10,000 entity transform updates

---

## Sprint 2: Scene Structure

### 2.1 SceneHandle
- [ ] Create `engine/core/scene/SceneHandle.hpp`
- [ ] Implement SceneHandle struct with id and generation fields
- [ ] Implement `isValid()` method
- [ ] Implement equality operators
- [ ] Implement std::hash specialization for SceneHandle

### 2.2 Scene Class
- [ ] Create `engine/core/scene/Scene.hpp`
- [ ] Create `engine/core/scene/Scene.cpp`
- [ ] Define SceneState enum (Unloaded, Loading, Active, Paused, Unloading)
- [ ] Implement Scene class with:
  - Constructor/destructor
  - Lifecycle methods (onLoad, onUnload, onActivate, onDeactivate, onPause, onResume)
  - Entity management methods (createEntity, destroyEntity, reparent)
  - Query methods (getRoot, getName, getState)
  - Iteration methods (forEachEntity, forEachRootEntity)
  - Serialization methods (serialize, deserialize)
- [ ] Implement root entity management
- [ ] Track all entities and root entities in scene
- [ ] Test Scene lifecycle and entity management

### 2.3 SceneGraph Utilities
- [ ] Create `engine/core/scene/SceneGraph.hpp`
- [ ] Create `engine/core/scene/SceneGraph.cpp`
- [ ] Implement hierarchy manipulation functions:
  - `setParent()`
  - `removeFromParent()`
  - `destroyRecursive()`
- [ ] Implement query functions:
  - `getParent()`
  - `getChildren()`
  - `getAncestors()`
  - `getDescendants()`
  - `findByPath()`
- [ ] Implement transform utility functions:
  - `getWorldPosition()`
  - `setWorldPosition()`
  - `localToWorld()`
  - `worldToLocal()`
- [ ] Implement traversal functions:
  - `traverseDepthFirst()`
  - `traverseBreadthFirst()`
- [ ] Test all SceneGraph utility functions

### 2.4 Scene Manager (Basic)
- [ ] Create `engine/core/scene/SceneManager.hpp`
- [ ] Create `engine/core/scene/SceneManager.cpp`
- [ ] Implement basic SceneManager class with:
  - Constructor taking World, AssetManager, EventBus
  - Scene storage (unordered_map of SceneHandle to Scene)
  - `loadScene()` method
  - `getScene()` method
  - `getActiveScene()` method
- [ ] Implement scene handle allocation system
- [ ] Integrate with AssetManager to load scene JSON files
- [ ] Migrate SceneLoader functionality to SceneManager

### 2.5 Update Scene JSON Format
- [ ] Update scene JSON format to support hierarchy:
  - Add "parent" field to entities
  - Add "children" array to entities
  - Add "id" field for entity references
- [ ] Update SceneSerializer (or create basic version) to handle hierarchy
- [ ] Test loading scenes with parent-child relationships

---

## Sprint 3: Scene Manager Features

### 3.1 Scene Stack
- [ ] Add scene stack vector to SceneManager
- [ ] Implement `pushScene()` method (for UI overlays, pause menus)
- [ ] Implement `popScene()` method
- [ ] Implement `loadSceneAdditive()` method (additive loading)
- [ ] Update scene activation/deactivation logic to handle stack
- [ ] Test scene stacking with main game + overlay

### 3.2 Scene State Management
- [ ] Implement scene state machine transitions
- [ ] Implement `pauseScene()` method
- [ ] Implement `resumeScene()` method
- [ ] Implement `setActiveScene()` method
- [ ] Ensure proper state transitions (Loading → Active → Paused, etc.)
- [ ] Test all state transitions

### 3.3 Scene Events
- [ ] Create `engine/core/scene/SceneEvents.hpp`
- [ ] Define scene event structs:
  - SceneLoadStarted
  - SceneLoadCompleted
  - SceneUnloadStarted
  - SceneUnloaded
  - SceneActivated
  - SceneDeactivated
  - SceneTransitionStarted
  - SceneTransitionCompleted
- [ ] Add scene events to Event.hpp variant
- [ ] Emit events from SceneManager at appropriate times
- [ ] Test event emission for all scene operations

### 3.4 Scene Manager Update Loop
- [ ] Implement `update()` method in SceneManager
- [ ] Integrate SceneManager::update() into Application::run()
- [ ] Handle scene lifecycle updates
- [ ] Test frame-by-frame scene management

### 3.5 Integration Tests
- [ ] Write integration test for multi-scene additive loading
- [ ] Write integration test for scene stacking
- [ ] Write integration test for scene state transitions
- [ ] Verify no memory leaks when loading/unloading scenes

---

## Sprint 4: Scene Transitions

### 4.1 SceneTransition Base Class
- [ ] Create `engine/core/scene/transitions/SceneTransition.hpp`
- [ ] Create `engine/core/scene/transitions/SceneTransition.cpp`
- [ ] Define TransitionPhase enum (Out, In)
- [ ] Implement SceneTransition base class:
  - Constructor/destructor
  - Non-copyable semantics
  - `update()` method
  - `isComplete()` method
  - `getProgress()`, `getPhase()`, `getDuration()` methods
  - Virtual `render()` method
  - Protected `onPhaseComplete()` method
- [ ] Test base transition lifecycle

### 4.2 FadeTransition
- [ ] Create `engine/core/scene/transitions/FadeTransition.hpp`
- [ ] Create `engine/core/scene/transitions/FadeTransition.cpp`
- [ ] Implement FadeTransition class extending SceneTransition
- [ ] Implement `render()` method for fade effect
- [ ] Test fade transition between scenes

### 4.3 SlideTransition
- [ ] Create `engine/core/scene/transitions/SlideTransition.hpp`
- [ ] Create `engine/core/scene/transitions/SlideTransition.cpp`
- [ ] Define SlideDirection enum (Left, Right, Up, Down)
- [ ] Implement SlideTransition class extending SceneTransition
- [ ] Implement `render()` method for slide effect
- [ ] Implement `getOldSceneOffset()` and `getNewSceneOffset()` methods
- [ ] Test slide transitions in all directions

### 4.4 CrossFadeTransition
- [ ] Create `engine/core/scene/transitions/CrossFadeTransition.hpp`
- [ ] Create `engine/core/scene/transitions/CrossFadeTransition.cpp`
- [ ] Implement CrossFadeTransition class extending SceneTransition
- [ ] Implement `getOldSceneAlpha()` and `getNewSceneAlpha()` methods
- [ ] Test crossfade transition

### 4.5 SceneManager Transition Integration
- [ ] Add transition state to SceneManager:
  - `_activeTransition` unique_ptr
  - `_transitionFrom` and `_transitionTo` SceneHandles
- [ ] Implement `switchScene()` overload with transition parameter
- [ ] Implement `processTransition()` method
- [ ] Implement `finalizeSceneSwitch()` method
- [ ] Update SceneManager::update() to handle transitions
- [ ] Test scene switching with all transition types

### 4.6 Renderer Integration for Transitions
- [ ] Add render target buffers to Renderer2D (_sceneABuffer, _sceneBBuffer)
- [ ] Implement `beginScene()` and `endScene()` in Renderer2D
- [ ] Implement `renderTransition()` method in Renderer2D
- [ ] Update render loop to handle transition rendering
- [ ] Test rendering with transitions

### 4.7 ShaderTransition Placeholder (Optional)
- [ ] Create `engine/core/scene/transitions/ShaderTransition.hpp` (placeholder)
- [ ] Define interface for future shader-based transitions
- [ ] Document planned JSON format for shader transitions

---

## Sprint 5: Serialization & Save System

### 5.1 Component Serialization Registration
- [ ] Update ComponentInfo struct to include serializeFn
- [ ] Update ComponentRegistry to store serialize functions
- [ ] Update World::registerComponent() to accept serializer function
- [ ] Update existing component registrations with serializers
- [ ] Test component serialization for all registered components

### 5.2 SceneSerializer
- [ ] Create `engine/core/scene/SceneSerializer.hpp`
- [ ] Create `engine/core/scene/SceneSerializer.cpp`
- [ ] Implement SceneSerializer class:
  - Constructor taking World and AssetManager
  - `serializeScene()` method
  - `deserializeScene()` method
  - `serializeEntity()` method
  - `deserializeEntity()` method
  - `serializeComponent()` method
  - `deserializeComponent()` method
  - Entity ID mapping for references
  - `buildEntityIdMap()` helper
  - `resolveEntityRef()` helper
- [ ] Handle parent-child relationships in serialization
- [ ] Handle entity references by ID/name
- [ ] Test serialization round-trip (save → load → verify)

### 5.3 Enhanced Scene JSON Format
- [ ] Update scene JSON format to include:
  - "name" and "version" fields
  - "settings" object (backgroundColor, gravity, bounds)
  - Entity hierarchy in "entities" array
  - "prefabInstances" array (future feature)
- [ ] Update SceneSerializer to handle new format
- [ ] Test loading enhanced scene format

### 5.4 SaveData Structure
- [ ] Create `engine/core/save/SaveData.hpp`
- [ ] Define SaveMetadata struct (saveName, timestamp, gameVersion, screenshotPath, playTimeSeconds, currentScene)
- [ ] Define SaveData struct (metadata, sceneState, persistentState, gameSettings)
- [ ] Implement serialization/deserialization for SaveData

### 5.5 SaveManager
- [ ] Create `engine/core/save/SaveManager.hpp`
- [ ] Create `engine/core/save/SaveManager.cpp`
- [ ] Implement SaveManager class:
  - Constructor taking SceneManager and World
  - Save operations (save, saveToFile, quickSave, autoSave)
  - Load operations (load, loadFromFile, quickLoad)
  - Save slot management (listSaves, deleteSave, renameSave, copySave)
  - Persistent state management (setPersistent, getPersistent, hasPersistent, clearPersistent)
  - Configuration methods (setSaveDirectory, setAutoSaveInterval, enableAutoSave)
  - `buildSaveData()` helper
  - `applySaveData()` helper
  - `getSavePath()` helper
- [ ] Implement auto-save timer functionality
- [ ] Test save/load operations

### 5.6 Saveable Component Marker
- [ ] Define SaveableComponent concept/trait
- [ ] Update component system to check saveable flag
- [ ] Mark appropriate components as saveable (e.g., PlayerStats)
- [ ] Mark non-saveable components (e.g., ParticleEmitter)
- [ ] Test save filtering by saveable flag

### 5.7 Integration Tests
- [ ] Write integration test: Load scene → modify → save → reload → verify
- [ ] Write integration test: Save/load with complex hierarchy
- [ ] Write integration test: Persistent state across scene transitions
- [ ] Write performance test: Large save file serialization

---

## Sprint 6: Integration & Polish

### 6.1 Application Integration
- [ ] Replace SceneLoader with SceneManager in Application
- [ ] Add SaveManager to Application
- [ ] Add getSceneManager() and getSaveManager() accessors
- [ ] Update Application::run() to call SceneManager::update()
- [ ] Update Application initialization to use SceneManager
- [ ] Test full application integration

### 6.2 World Scene Tracking
- [ ] Add entity-to-scene mapping to World class
- [ ] Implement `setEntityScene()` method
- [ ] Implement `getEntityScene()` method
- [ ] Implement `getEntitiesInScene()` method
- [ ] Update SceneManager to set entity scenes
- [ ] Test scene-based entity queries

### 6.3 Renderer Integration
- [ ] Ensure Renderer2D supports scene-based rendering
- [ ] Test rendering with multiple scenes
- [ ] Test rendering during transitions
- [ ] Performance test: Rendering with 10,000 entities

### 6.4 Unit Tests
- [ ] Complete unit test coverage for all new classes
- [ ] Test edge cases (empty scenes, deep hierarchies, rapid transitions)
- [ ] Test error handling (invalid handles, missing files, etc.)

### 6.5 Integration Tests
- [ ] End-to-end test: Full game loop with scene management
- [ ] Test scene transitions with entity state preservation
- [ ] Test save/load with scene transitions
- [ ] Memory leak tests for all operations

### 6.6 Performance Tests
- [ ] 10,000 entities with hierarchy updates at 60 FPS
- [ ] Deep hierarchy (100 levels) transform propagation
- [ ] Rapid scene switching performance
- [ ] Large save file serialization performance
- [ ] Profile and optimize hotspots

### 6.7 Documentation
- [ ] Document SceneManager API
- [ ] Document SceneGraph utilities
- [ ] Document save/load system
- [ ] Create example scenes demonstrating all features
- [ ] Update README with new features

### 6.8 Demo Scene
- [ ] Create comprehensive demo scene with:
  - Hierarchical entity structures
  - Scene transitions
  - Save/load functionality
  - Multiple scenes with stacking
- [ ] Document demo scene usage

---

## Future Considerations (Not in Current Scope)

- Prefab system (first-class vs templates)
- Async scene loading
- Scene streaming (distance-based loading)
- Editor integration (hierarchy window, entity inspector)
- Networking (multi-player scene sync)
- ShaderTransition full implementation

---

## Success Criteria Checklist

- [ ] Can create parent-child entity relationships in code and JSON
- [ ] Child transforms update when parent moves
- [ ] Can load/unload scenes without memory leaks
- [ ] Can stack scenes (main game + pause menu overlay)
- [ ] Fade transition works between scenes
- [ ] Can save game state and restore it exactly
- [ ] 10,000 entities with hierarchy updates at 60 FPS
- [ ] All scene operations emit appropriate events
