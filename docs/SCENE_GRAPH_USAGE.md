# Scene Graph Usage Guide

## Quick Start

### Loading a Scene

```cpp
// Get SceneManager from Application
SceneManager& scenes = app.getSceneManager();

// Load a scene
SceneHandle handle = scenes.loadScene("scenes/level1.scene.json");
if (handle.isValid()) {
    Scene* scene = scenes.getScene(handle);
    // Scene is now loaded and active
}
```

### Creating Entities in a Scene

```cpp
Scene* scene = scenes.getActiveScene();
if (scene) {
    // Create a root entity
    EntityId player = scene->createEntity();
    
    // Create a child entity
    EntityId weapon = scene->createEntity(player);
    
    // Add components
    world.addComponent<Transform2D>(player, glm::vec2{100, 200});
    world.addComponent<Transform2D>(weapon, glm::vec2{20, 0});
}
```

### Switching Scenes with Transitions

```cpp
// Fade transition
scenes.switchSceneFade("scenes/level2.scene.json", 1.0f);

// Slide transition
scenes.switchSceneSlide("scenes/menu.scene.json", 0.5f, SlideDirection::Right);

// Crossfade transition
scenes.switchSceneCrossFade("scenes/level3.scene.json", 1.5f);
```

### Using Scene Stack (UI Overlays)

```cpp
// Load main game scene
SceneHandle game = scenes.loadScene("scenes/game.scene.json");

// Push pause menu (pauses game scene)
SceneHandle pause = scenes.pushScene("scenes/pause_menu.scene.json");

// Pop pause menu (resumes game scene)
scenes.popScene();
```

### Saving and Loading

```cpp
// Get SaveManager from Application
SaveManager& saves = app.getSaveManager();

// Save game
saves.save("save_slot_1");

// Load game
saves.load("save_slot_1");

// Quick save/load
saves.quickSave();
saves.quickLoad();

// Persistent state (survives scene transitions)
saves.setPersistent("playerGold", nlohmann::json{1000});
int gold = saves.getPersistent("playerGold").get<int>();
```

### Entity Hierarchy

```cpp
// Get parent
EntityId parent = SceneGraph::getParent(world, entity);

// Get children
std::vector<EntityId> children = SceneGraph::getChildren(world, entity);

// Reparent
SceneGraph::setParent(world, child, newParent);

// World position
glm::vec2 worldPos = SceneGraph::getWorldPosition(world, entity);
SceneGraph::setWorldPosition(world, entity, glm::vec2{200, 300});
```

### Querying Entities by Scene

```cpp
// Get all entities in a scene
SceneHandle sceneHandle = scenes.getActiveSceneHandle();
std::vector<EntityId> entities = world.getEntitiesInScene(sceneHandle);

// Get which scene an entity belongs to
SceneHandle entityScene = world.getEntityScene(entity);
```

---

## Common Patterns

### Pattern 1: Main Menu → Game

```cpp
// Load menu
SceneHandle menu = scenes.loadScene("scenes/main_menu.scene.json");

// When "Start Game" is clicked:
scenes.switchSceneFade("scenes/game.scene.json", 0.5f);
```

### Pattern 2: Game with Pause Menu

```cpp
// Load game
SceneHandle game = scenes.loadScene("scenes/game.scene.json");

// On pause key press:
SceneHandle pause = scenes.pushScene("scenes/pause_menu.scene.json");

// On resume:
scenes.popScene();
```

### Pattern 3: Save Before Scene Switch

```cpp
// Save current state
saves.quickSave();

// Switch scene
scenes.switchScene("scenes/next_level.scene.json");
```

### Pattern 4: Persistent Inventory

```cpp
// Store inventory in persistent state
nlohmann::json inventory = {
    {"sword", 1},
    {"potion", 5}
};
saves.setPersistent("inventory", inventory);

// Retrieve later (even after scene switch)
nlohmann::json inv = saves.getPersistent("inventory");
```

---

## Scene JSON Examples

### Simple Scene

```json
{
  "name": "Test Scene",
  "version": "1.0",
  "entities": [
    {
      "id": "player",
      "name": "Player",
      "components": {
        "Transform2D": {
          "position": [400, 300],
          "rotation": 0,
          "scale": [1, 1]
        }
      }
    }
  ]
}
```

### Hierarchical Scene

```json
{
  "name": "Hierarchy Demo",
  "entities": [
    {
      "id": "parent",
      "name": "Parent",
      "parent": null,
      "components": {
        "Transform2D": {
          "position": [100, 100],
          "rotation": 0,
          "scale": [1, 1]
        }
      }
    },
    {
      "id": "child",
      "name": "Child",
      "parent": "parent",
      "components": {
        "Transform2D": {
          "position": [50, 0],
          "rotation": 0,
          "scale": [0.5, 0.5]
        }
      }
    }
  ]
}
```

---

## Best Practices

1. **Always check handle validity** before using scenes
2. **Use scene stack for UI overlays** - don't unload main scene
3. **Save before major transitions** - use quick save
4. **Use persistent state sparingly** - only for truly cross-scene data
5. **Clean up entities properly** - scenes handle this automatically
6. **Use transitions for smooth scene switches** - improves UX
7. **Track entities by scene** - use `World::getEntitiesInScene()` for scene-specific queries

---

## Troubleshooting

### Scene Not Loading

- Check file path is correct
- Verify JSON is valid
- Check that components are registered
- Look for error logs

### Entities Not Appearing

- Verify entities are created in scene (not directly in World)
- Check that entities have Transform2D component
- Ensure scene is active
- Verify renderer is rendering the scene

### Transitions Not Working

- Ensure SceneManager::update() is called every frame
- Check that transition is not null
- Verify transition is not already complete
- Verify renderer supports transition rendering

### Save/Load Issues

- Check save directory exists and is writable
- Verify JSON serialization works for all components
- Ensure scene is loaded before saving
- Check that scene path in save file is correct
