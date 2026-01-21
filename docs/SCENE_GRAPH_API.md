# Scene Graph API Documentation

## Overview

The Scene Graph system provides hierarchical entity management, scene loading/unloading, transitions, and save/load functionality. This document describes the public API for all scene graph components.

---

## SceneManager

The `SceneManager` class manages scene loading, unloading, state, and transitions.

### Scene Loading

#### `loadScene(path) -> SceneHandle`

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

#### `unloadScene(handle)`

Unloads a scene and destroys all its entities.

**Parameters:**
- `handle` (SceneHandle): Handle of scene to unload

#### `unloadAllScenes()`

Unloads all currently loaded scenes.

### Scene Stack

#### `pushScene(path) -> SceneHandle`

Pushes a scene onto the stack, making it active. Previous top scene is paused.

**Parameters:**
- `path` (std::filesystem::path): Path to scene JSON file

**Returns:**
- `SceneHandle`: Handle to pushed scene

#### `popScene() -> SceneHandle`

Pops the top scene from the stack. Scene below becomes active.

**Returns:**
- `SceneHandle`: Handle of popped scene, or invalid if stack was empty

### Scene State Management

#### `pauseScene(handle)`

Pauses an active scene.

**Parameters:**
- `handle` (SceneHandle): Scene to pause

#### `resumeScene(handle)`

Resumes a paused scene.

**Parameters:**
- `handle` (SceneHandle): Scene to resume

#### `setActiveScene(handle)`

Sets the active scene. Previous active scene is deactivated.

**Parameters:**
- `handle` (SceneHandle): Scene to make active

### Scene Switching

#### `switchScene(path) -> SceneHandle`

Switches to a new scene, unloading the current scene.

**Parameters:**
- `path` (std::filesystem::path): Path to new scene JSON file

**Returns:**
- `SceneHandle`: Handle to new scene

#### `switchSceneFade(path, duration, color)`

Switches to a new scene with a fade transition.

**Parameters:**
- `path` (std::filesystem::path): Path to new scene
- `duration` (float): Transition duration in seconds (default: 0.5)
- `color` (glm::vec4): Fade color (default: black)

#### `switchSceneSlide(path, duration, direction)`

Switches to a new scene with a slide transition.

**Parameters:**
- `path` (std::filesystem::path): Path to new scene
- `duration` (float): Transition duration in seconds (default: 0.5)
- `direction` (SlideDirection): Slide direction (default: Left)

#### `switchSceneCrossFade(path, duration)`

Switches to a new scene with a crossfade transition.

**Parameters:**
- `path` (std::filesystem::path): Path to new scene
- `duration` (float): Transition duration in seconds (default: 0.5)

### Scene Queries

#### `getScene(handle) -> Scene*`

Gets a scene by handle.

**Parameters:**
- `handle` (SceneHandle): Scene handle

**Returns:**
- `Scene*`: Pointer to scene, or nullptr if not found

#### `getActiveScene() -> Scene*`

Gets the currently active scene.

**Returns:**
- `Scene*`: Pointer to active scene, or nullptr if none

#### `getActiveSceneHandle() -> SceneHandle`

Gets the handle of the active scene.

**Returns:**
- `SceneHandle`: Active scene handle, or invalid if none

#### `getLoadedScenes() -> std::vector<SceneHandle>`

Gets all loaded scene handles.

**Returns:**
- `std::vector<SceneHandle>`: Vector of scene handles

### Update Loop

#### `update(dt)`

Updates scene manager (call every frame). Handles transitions and deferred operations.

**Parameters:**
- `dt` (float): Delta time since last frame (seconds)

---

## Scene

The `Scene` class manages a collection of entities within a scene context.

### Lifecycle Methods

#### `onLoad()`

Called when scene is being loaded. Creates the root entity.

#### `onUnload()`

Called when scene is being unloaded. Destroys all entities.

#### `onActivate()`

Called when scene becomes active.

#### `onDeactivate()`

Called when scene becomes inactive.

#### `onPause()`

Called when scene is paused.

#### `onResume()`

Called when scene is resumed from pause.

### Entity Management

#### `createEntity() -> EntityId`

Creates a new entity in this scene. Entity is automatically a child of the scene root.

**Returns:**
- `EntityId`: ID of created entity

#### `createEntity(parent) -> EntityId`

Creates a new entity with a parent.

**Parameters:**
- `parent` (EntityId): Parent entity (must be in this scene)

**Returns:**
- `EntityId`: ID of created entity

#### `destroyEntity(id)`

Destroys an entity in this scene (recursively destroys children).

**Parameters:**
- `id` (EntityId): Entity to destroy

#### `reparent(entity, newParent)`

Reparents an entity within the scene.

**Parameters:**
- `entity` (EntityId): Entity to reparent
- `newParent` (EntityId): New parent (or invalid for root)

### Queries

#### `getRoot() -> EntityId`

Gets the invisible root entity of this scene.

**Returns:**
- `EntityId`: Root entity ID

#### `getName() -> const std::string&`

Gets the name of this scene.

**Returns:**
- `const std::string&`: Scene name

#### `getState() -> SceneState`

Gets the current state of this scene.

**Returns:**
- `SceneState`: Current state (Unloaded, Loading, Active, Paused, Unloading)

#### `hasEntity(id) -> bool`

Checks if an entity belongs to this scene.

**Parameters:**
- `id` (EntityId): Entity ID

**Returns:**
- `bool`: True if entity is in this scene

---

## SceneGraph

The `SceneGraph` namespace provides utility functions for hierarchy manipulation.

### Hierarchy Manipulation

#### `setParent(world, child, parent)`

Sets the parent of an entity.

**Parameters:**
- `world` (World&): ECS World
- `child` (EntityId): Entity to reparent
- `parent` (EntityId): New parent (or invalid for root)

#### `getParent(world, entity) -> EntityId`

Gets the parent of an entity.

**Parameters:**
- `world` (World&): ECS World
- `entity` (EntityId): Entity

**Returns:**
- `EntityId`: Parent entity ID, or invalid if root

#### `getChildren(world, entity) -> std::vector<EntityId>`

Gets all direct children of an entity.

**Parameters:**
- `world` (World&): ECS World
- `entity` (EntityId): Entity

**Returns:**
- `std::vector<EntityId>`: Vector of child entity IDs

#### `destroyRecursive(world, entity)`

Destroys an entity and all its descendants recursively.

**Parameters:**
- `world` (World&): ECS World
- `entity` (EntityId): Entity to destroy

### Transform Utilities

#### `getWorldPosition(world, entity) -> glm::vec2`

Gets the world position of an entity.

**Parameters:**
- `world` (World&): ECS World
- `entity` (EntityId): Entity

**Returns:**
- `glm::vec2`: World position

#### `setWorldPosition(world, entity, worldPos)`

Sets the world position of an entity.

**Parameters:**
- `world` (World&): ECS World
- `entity` (EntityId): Entity
- `worldPos` (glm::vec2): World position

#### `localToWorld(world, entity, localPos) -> glm::vec2`

Converts a local position to world coordinates.

**Parameters:**
- `world` (World&): ECS World
- `entity` (EntityId): Entity
- `localPos` (glm::vec2): Local position

**Returns:**
- `glm::vec2`: World position

#### `worldToLocal(world, entity, worldPos) -> glm::vec2`

Converts a world position to local coordinates.

**Parameters:**
- `world` (World&): ECS World
- `entity` (EntityId): Entity
- `worldPos` (glm::vec2): World position

**Returns:**
- `glm::vec2`: Local position

---

## SaveManager

The `SaveManager` class handles saving and loading game state.

### Save Operations

#### `save(slotName) -> bool`

Saves the game to a named slot.

**Parameters:**
- `slotName` (std::string): Name of the save slot

**Returns:**
- `bool`: True on success, false on failure

#### `saveToFile(path) -> bool`

Saves the game to a specific file path.

**Parameters:**
- `path` (std::filesystem::path): Path to save file

**Returns:**
- `bool`: True on success, false on failure

#### `quickSave() -> bool`

Performs a quick save (to a special quick-save slot).

**Returns:**
- `bool`: True on success

#### `autoSave() -> bool`

Performs an auto-save (if enabled).

**Returns:**
- `bool`: True on success

### Load Operations

#### `load(slotName) -> bool`

Loads a game from a named slot.

**Parameters:**
- `slotName` (std::string): Name of the save slot

**Returns:**
- `bool`: True on success, false on failure

#### `loadFromFile(path) -> bool`

Loads a game from a specific file path.

**Parameters:**
- `path` (std::filesystem::path): Path to save file

**Returns:**
- `bool`: True on success, false on failure

#### `quickLoad() -> bool`

Loads the quick-save slot.

**Returns:**
- `bool`: True on success

### Persistent State

#### `setPersistent(key, value)`

Sets a persistent state value (survives scene transitions).

**Parameters:**
- `key` (std::string): Key name
- `value` (nlohmann::json): JSON value

#### `getPersistent(key) -> nlohmann::json`

Gets a persistent state value.

**Parameters:**
- `key` (std::string): Key name

**Returns:**
- `nlohmann::json`: JSON value, or null if not found

#### `hasPersistent(key) -> bool`

Checks if a persistent state key exists.

**Parameters:**
- `key` (std::string): Key name

**Returns:**
- `bool`: True if key exists

---

## World Scene Tracking

The `World` class provides scene tracking for entities.

### Scene Tracking API

#### `setEntityScene(id, scene)`

Sets which scene an entity belongs to.

**Parameters:**
- `id` (EntityId): Entity ID
- `scene` (SceneHandle): Scene handle (or invalid for no scene)

#### `getEntityScene(id) -> SceneHandle`

Gets which scene an entity belongs to.

**Parameters:**
- `id` (EntityId): Entity ID

**Returns:**
- `SceneHandle`: Scene handle, or invalid if entity has no scene

#### `getEntitiesInScene(scene) -> std::vector<EntityId>`

Gets all entities in a specific scene.

**Parameters:**
- `scene` (SceneHandle): Scene handle

**Returns:**
- `std::vector<EntityId>`: Vector of entity IDs in the scene

---

## Scene Transitions

### FadeTransition

Fades the old scene to a color, then fades in the new scene.

**Example:**
```cpp
auto fade = std::make_unique<FadeTransition>(1.0f, glm::vec4{0.0f, 0.0f, 0.0f, 1.0f});
sceneManager.switchScene("new_scene.json", std::move(fade));
```

### SlideTransition

Slides the old scene out while sliding the new scene in.

**Example:**
```cpp
auto slide = std::make_unique<SlideTransition>(1.0f, SlideDirection::Left);
sceneManager.switchScene("new_scene.json", std::move(slide));
```

### CrossFadeTransition

Fades out the old scene while fading in the new scene simultaneously.

**Example:**
```cpp
auto crossfade = std::make_unique<CrossFadeTransition>(1.0f);
sceneManager.switchScene("new_scene.json", std::move(crossfade));
```

---

## Scene JSON Format

Scenes are defined in JSON files with the following structure:

```json
{
  "name": "Level 1",
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
          "position": [100, 200],
          "rotation": 0,
          "scale": [1, 1]
        }
      }
    }
  ]
}
```

---

## Best Practices

1. **Always use SceneManager for scene operations** - Don't create entities directly in World if they belong to a scene
2. **Use SceneHandle for scene references** - Never store raw Scene pointers
3. **Check handle validity** - Always check `handle.isValid()` before using
4. **Clean up on unload** - Scenes automatically clean up entities, but ensure components are properly destroyed
5. **Use persistent state for cross-scene data** - Don't store scene-specific data in persistent state
6. **Save before major transitions** - Use quick save before switching scenes in production

---

## Error Handling

- Invalid scene handles return `nullptr` from `getScene()`
- Invalid entity operations throw `std::runtime_error`
- Failed scene loads return invalid `SceneHandle`
- Save/load operations return `false` on failure (check return values)
