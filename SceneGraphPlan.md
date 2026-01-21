# Scene Graph & Scene Management System Plan

## Overview

This document outlines the implementation plan for a robust 2D scene graph system, enhanced scene management, scene transitions, and save/load serialization for the Journeyman Engine.

---

## Current State Analysis

**What exists:**
- `SceneLoader` - Basic JSON parsing, creates flat entity list
- `World` - ECS with tags, components, systems
- `EntityId` - Generational indices
- Component registration with JSON deserializers

**What's missing:**
- Hierarchical parent-child relationships (scene graph)
- Transform propagation (local → world)
- Scene lifecycle management (load, unload, pause)
- Multi-scene support (additive loading, scene stacking)
- Scene transitions
- Game state serialization (save/load)

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        SceneManager                             │
│  - Active scenes stack                                          │
│  - Scene transitions                                            │
│  - Load/unload orchestration                                    │
└─────────────────────────────────────────────────────────────────┘
        │                    │                      │
        ▼                    ▼                      ▼
┌──────────────┐    ┌──────────────┐    ┌────────────────────┐
│    Scene     │    │    Scene     │    │ SceneTransition    │
│  - Entities  │    │  - Entities  │    │ - Progress [0,1]   │
│  - Root nodes│    │  - Root nodes│    │ - Shader (future)  │
│  - State     │    │  - State     │    │ - Duration         │
└──────────────┘    └──────────────┘    └────────────────────┘
        │
        ▼
┌─────────────────────────────────────────────────────────────────┐
│                     SceneGraph (per scene)                      │
│  - Root entity                                                  │
│  - Parent-child relationships                                   │
│  - Transform hierarchy                                          │
└─────────────────────────────────────────────────────────────────┘
        │
        ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Transform System                             │
│  - Local transforms (position, rotation, scale)                 │
│  - World transform cache                                        │
│  - Dirty flag propagation                                       │
└─────────────────────────────────────────────────────────────────┘
```

---

## Phase 1: Core Components & Transform Hierarchy

### 1.1 Transform2D Component

**File:** `engine/core/components/Transform2D.hpp`

```cpp
struct Transform2D : Component<Transform2D> {
    COMPONENT_NAME("Transform2D");

    // Local transform (relative to parent)
    glm::vec2 position{0.0f, 0.0f};
    float rotation{0.0f};           // radians
    glm::vec2 scale{1.0f, 1.0f};

    // Cached world transform (computed by TransformSystem)
    glm::mat3 localMatrix{1.0f};
    glm::mat3 worldMatrix{1.0f};

    // Hierarchy
    EntityId parent{0, 0};          // Invalid = root
    std::vector<EntityId> children;

    // Dirty tracking
    bool dirty{true};
    int32_t depth{0};               // For sorting (root = 0)
};
```

**Key decisions:**
- Use `glm::mat3` for 2D (3x3 handles translation in homogeneous coords)
- Store both local and world matrices to avoid recomputation
- Depth field enables topological sort without tree traversal

### 1.2 Hierarchy Component (Optional Separation)

For cleaner separation of concerns, consider splitting hierarchy from transform:

**File:** `engine/core/components/Hierarchy.hpp`

```cpp
struct Hierarchy : Component<Hierarchy> {
    COMPONENT_NAME("Hierarchy");

    EntityId parent{0, 0};
    EntityId firstChild{0, 0};
    EntityId nextSibling{0, 0};
    EntityId prevSibling{0, 0};
    int32_t depth{0};
};
```

**Trade-off:** Linked-list siblings are cache-unfriendly but O(1) insert/remove. Vector children are cache-friendly for iteration but O(n) remove. Recommend vector for 2D games (fewer deep hierarchies).

### 1.3 TransformSystem

**File:** `engine/core/systems/TransformSystem.hpp`

```cpp
class TransformSystem : public System {
public:
    using Traits = SystemTraits<TransformSystem>;

    // Traits
    struct TransformUpdateTag {};
    using Provides = TypeList<TransformUpdateTag>;
    using Writes = TypeList<Transform2D>;
    using Reads = TypeList<>;
    using DependsOn = TypeList<>;  // Runs early

    void update(World& world, float dt) override;

private:
    void propagateTransforms(World& world);
    void updateWorldMatrix(Transform2D& transform, const glm::mat3& parentWorld);
};
```

**Algorithm:**
1. Collect all dirty transforms
2. Sort by depth (ensures parents update before children)
3. For each dirty transform:
   - Compute local matrix from position/rotation/scale
   - Multiply by parent's world matrix
   - Mark children as dirty
   - Clear dirty flag

**Optimization:** Use a dirty root list - only traverse subtrees with changes.

---

## Phase 2: Scene Graph Structure

### 2.1 SceneHandle

**File:** `engine/core/scene/SceneHandle.hpp`

```cpp
struct SceneHandle {
    uint32_t id{0};
    uint32_t generation{0};

    bool isValid() const { return id != 0; }
    bool operator==(const SceneHandle&) const = default;
};

namespace std {
    template<> struct hash<SceneHandle> { /* ... */ };
}
```

### 2.2 Scene Class

**File:** `engine/core/scene/Scene.hpp`

```cpp
enum class SceneState {
    Unloaded,
    Loading,
    Active,
    Paused,
    Unloading
};

class Scene {
public:
    explicit Scene(std::string name);
    ~Scene();

    // Lifecycle
    void onLoad();
    void onUnload();
    void onActivate();
    void onDeactivate();
    void onPause();
    void onResume();

    // Entity management
    EntityId createEntity();
    EntityId createEntity(EntityId parent);
    void destroyEntity(EntityId id);
    void reparent(EntityId entity, EntityId newParent);

    // Queries
    EntityId getRoot() const { return _root; }
    const std::string& getName() const { return _name; }
    SceneState getState() const { return _state; }

    // Iteration
    template<typename Fn>
    void forEachEntity(Fn&& fn);

    template<typename Fn>
    void forEachRootEntity(Fn&& fn);

    // Serialization
    nlohmann::json serialize() const;
    void deserialize(const nlohmann::json& data);

private:
    std::string _name;
    SceneState _state{SceneState::Unloaded};
    EntityId _root;                              // Invisible root node
    std::unordered_set<EntityId> _entities;      // All entities in this scene
    std::unordered_set<EntityId> _rootEntities;  // Direct children of _root
};
```

### 2.3 SceneGraph Utilities

**File:** `engine/core/scene/SceneGraph.hpp`

```cpp
namespace SceneGraph {
    // Hierarchy manipulation
    void setParent(World& world, EntityId child, EntityId parent);
    void removeFromParent(World& world, EntityId entity);
    void destroyRecursive(World& world, EntityId entity);

    // Queries
    EntityId getParent(World& world, EntityId entity);
    std::vector<EntityId> getChildren(World& world, EntityId entity);
    std::vector<EntityId> getAncestors(World& world, EntityId entity);
    std::vector<EntityId> getDescendants(World& world, EntityId entity);
    EntityId findByPath(World& world, EntityId root, std::string_view path);

    // Transform utilities
    glm::vec2 getWorldPosition(World& world, EntityId entity);
    void setWorldPosition(World& world, EntityId entity, glm::vec2 pos);
    glm::vec2 localToWorld(World& world, EntityId entity, glm::vec2 localPos);
    glm::vec2 worldToLocal(World& world, EntityId entity, glm::vec2 worldPos);

    // Traversal
    template<typename Fn>
    void traverseDepthFirst(World& world, EntityId root, Fn&& fn);

    template<typename Fn>
    void traverseBreadthFirst(World& world, EntityId root, Fn&& fn);
}
```

---

## Phase 3: Scene Manager

### 3.1 SceneManager Class

**File:** `engine/core/scene/SceneManager.hpp`

```cpp
class SceneManager {
public:
    explicit SceneManager(World& world, AssetManager& assets, EventBus& events);

    // Scene loading
    SceneHandle loadScene(const std::filesystem::path& path);
    SceneHandle loadSceneAdditive(const std::filesystem::path& path);
    void unloadScene(SceneHandle handle);
    void unloadAllScenes();

    // Scene switching
    void switchScene(const std::filesystem::path& path);
    void switchScene(const std::filesystem::path& path, SceneTransition transition);

    // Scene stack (for UI overlays, pause menus, etc.)
    void pushScene(const std::filesystem::path& path);
    void popScene();

    // Scene state
    void pauseScene(SceneHandle handle);
    void resumeScene(SceneHandle handle);
    void setActiveScene(SceneHandle handle);

    // Queries
    Scene* getScene(SceneHandle handle);
    Scene* getActiveScene();
    SceneHandle getActiveSceneHandle() const;
    std::vector<SceneHandle> getLoadedScenes() const;

    // Frame update (call from Application)
    void update(float dt);

private:
    World& _world;
    AssetManager& _assets;
    EventBus& _events;

    std::unordered_map<SceneHandle, std::unique_ptr<Scene>> _scenes;
    std::vector<SceneHandle> _sceneStack;          // Bottom = main, top = overlay
    SceneHandle _activeScene;
    uint32_t _nextSceneId{1};
    uint32_t _sceneGeneration{0};

    // Transition state
    std::unique_ptr<SceneTransition> _activeTransition;
    SceneHandle _transitionFrom;
    SceneHandle _transitionTo;

    SceneHandle allocateHandle();
    void processTransition(float dt);
    void finalizeSceneSwitch();
};
```

### 3.2 Scene Events

**File:** `engine/core/scene/SceneEvents.hpp`

```cpp
namespace events {
    struct SceneLoadStarted {
        SceneHandle handle;
        std::string sceneName;
    };

    struct SceneLoadCompleted {
        SceneHandle handle;
        std::string sceneName;
    };

    struct SceneUnloadStarted {
        SceneHandle handle;
        std::string sceneName;
    };

    struct SceneUnloaded {
        SceneHandle handle;
        std::string sceneName;
    };

    struct SceneActivated {
        SceneHandle handle;
        std::string sceneName;
    };

    struct SceneDeactivated {
        SceneHandle handle;
        std::string sceneName;
    };

    struct SceneTransitionStarted {
        SceneHandle from;
        SceneHandle to;
        std::string transitionType;
    };

    struct SceneTransitionCompleted {
        SceneHandle from;
        SceneHandle to;
    };
}
```

Add these to your `Event.hpp` variant:

```cpp
using Event = std::variant<
    events::WindowResized,
    events::Quit,
    events::DynamicEvent,
    events::SceneLoadStarted,
    events::SceneLoadCompleted,
    events::SceneUnloadStarted,
    events::SceneUnloaded,
    events::SceneActivated,
    events::SceneDeactivated,
    events::SceneTransitionStarted,
    events::SceneTransitionCompleted
>;
```

---

## Phase 4: Scene Transitions

### 4.1 Transition Base Class

**File:** `engine/core/scene/transitions/SceneTransition.hpp`

```cpp
enum class TransitionPhase {
    Out,    // Fading out old scene
    In      // Fading in new scene
};

class SceneTransition {
public:
    explicit SceneTransition(float duration);
    virtual ~SceneTransition() = default;

    // Non-copyable
    SceneTransition(const SceneTransition&) = delete;
    SceneTransition& operator=(const SceneTransition&) = delete;

    void update(float dt);
    bool isComplete() const;

    float getProgress() const { return _progress; }
    TransitionPhase getPhase() const { return _phase; }
    float getDuration() const { return _duration; }

    // Override for custom rendering
    virtual void render(Renderer2D& renderer, int screenWidth, int screenHeight);

protected:
    virtual void onPhaseComplete(TransitionPhase completed);

    float _duration;
    float _elapsed{0.0f};
    float _progress{0.0f};          // 0.0 to 1.0 within current phase
    TransitionPhase _phase{TransitionPhase::Out};
    bool _complete{false};
};
```

### 4.2 Built-in Transitions

**File:** `engine/core/scene/transitions/FadeTransition.hpp`

```cpp
class FadeTransition : public SceneTransition {
public:
    FadeTransition(float duration, glm::vec4 color = {0, 0, 0, 1});

    void render(Renderer2D& renderer, int screenWidth, int screenHeight) override;

private:
    glm::vec4 _color;
};
```

**File:** `engine/core/scene/transitions/SlideTransition.hpp`

```cpp
enum class SlideDirection { Left, Right, Up, Down };

class SlideTransition : public SceneTransition {
public:
    SlideTransition(float duration, SlideDirection direction);

    void render(Renderer2D& renderer, int screenWidth, int screenHeight) override;
    glm::vec2 getOldSceneOffset() const;
    glm::vec2 getNewSceneOffset() const;

private:
    SlideDirection _direction;
};
```

**File:** `engine/core/scene/transitions/CrossFadeTransition.hpp`

```cpp
class CrossFadeTransition : public SceneTransition {
public:
    explicit CrossFadeTransition(float duration);

    float getOldSceneAlpha() const;
    float getNewSceneAlpha() const;
};
```

### 4.3 Future: Shader-Based Transitions

**Planned JSON format:**

```json
{
    "name": "DiamondWipe",
    "type": "shader",
    "duration": 1.0,
    "shader": {
        "vertex": "assets/shaders/transitions/fullscreen.vert.glsl",
        "fragment": "assets/shaders/transitions/diamond_wipe.frag.glsl"
    },
    "uniforms": {
        "u_diamondSize": 0.1,
        "u_edgeSoftness": 0.02
    }
}
```

**Placeholder interface:**

```cpp
class ShaderTransition : public SceneTransition {
public:
    ShaderTransition(float duration, ShaderHandle shader);

    void setUniform(const std::string& name, float value);
    void setUniform(const std::string& name, glm::vec2 value);
    void setUniform(const std::string& name, glm::vec4 value);

    void render(Renderer2D& renderer, int screenWidth, int screenHeight) override;

private:
    ShaderHandle _shader;
    std::unordered_map<std::string, std::variant<float, glm::vec2, glm::vec4>> _uniforms;
};
```

---

## Phase 5: Scene Serialization

### 5.1 Enhanced Scene JSON Format

```json
{
    "name": "Level 1",
    "version": "1.0",
    "settings": {
        "backgroundColor": [0.1, 0.1, 0.15, 1.0],
        "gravity": [0, -9.8],
        "bounds": { "min": [-1000, -1000], "max": [1000, 1000] }
    },
    "entities": [
        {
            "id": "player",
            "name": "Player",
            "parent": null,
            "children": ["player_sprite", "player_hitbox"],
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
                    "texture": "assets/textures/player.png",
                    "layer": 10
                }
            }
        }
    ],
    "prefabInstances": [
        {
            "prefab": "assets/prefabs/enemy.prefab.json",
            "overrides": {
                "Transform2D.position": [500, 200]
            }
        }
    ]
}
```

### 5.2 SceneSerializer

**File:** `engine/core/scene/SceneSerializer.hpp`

```cpp
class SceneSerializer {
public:
    explicit SceneSerializer(World& world, AssetManager& assets);

    // Full scene serialization
    nlohmann::json serializeScene(const Scene& scene);
    void deserializeScene(Scene& scene, const nlohmann::json& data);

    // Single entity serialization
    nlohmann::json serializeEntity(EntityId entity, bool includeChildren = true);
    EntityId deserializeEntity(const nlohmann::json& data, EntityId parent = {});

    // Component serialization (uses registered serializers)
    nlohmann::json serializeComponent(EntityId entity, ComponentId componentId);
    void deserializeComponent(EntityId entity, const std::string& name, const nlohmann::json& data);

private:
    World& _world;
    AssetManager& _assets;

    // For resolving entity references during load
    std::unordered_map<std::string, EntityId> _idMap;

    void buildEntityIdMap(const nlohmann::json& entities);
    EntityId resolveEntityRef(const std::string& ref);
};
```

### 5.3 Component Serializer Registration

Extend `ComponentRegistry` to support serialization:

```cpp
// In ComponentInfo
struct ComponentInfo {
    ComponentId id;
    std::string name;
    std::function<void(World&, EntityId, const nlohmann::json&)> deserializeFn;
    std::function<nlohmann::json(World&, EntityId)> serializeFn;  // NEW
};

// Registration helper
template <ComponentType T>
void World::registerComponent(
    std::function<void(World&, EntityId, const nlohmann::json&)> deserializer,
    std::function<nlohmann::json(World&, EntityId)> serializer    // NEW
);
```

---

## Phase 6: Save Game System

### 6.1 SaveData Structure

**File:** `engine/core/save/SaveData.hpp`

```cpp
struct SaveMetadata {
    std::string saveName;
    std::string timestamp;
    std::string gameVersion;
    std::string screenshotPath;      // Optional thumbnail
    uint32_t playTimeSeconds;
    std::string currentScene;
};

struct SaveData {
    SaveMetadata metadata;
    nlohmann::json sceneState;       // Current scene serialized
    nlohmann::json persistentState;  // Cross-scene data (inventory, unlocks, etc.)
    nlohmann::json gameSettings;     // Player preferences
};
```

### 6.2 SaveManager

**File:** `engine/core/save/SaveManager.hpp`

```cpp
class SaveManager {
public:
    SaveManager(SceneManager& scenes, World& world);

    // Save operations
    bool save(const std::string& slotName);
    bool saveToFile(const std::filesystem::path& path);
    bool quickSave();
    bool autoSave();

    // Load operations
    bool load(const std::string& slotName);
    bool loadFromFile(const std::filesystem::path& path);
    bool quickLoad();

    // Save slot management
    std::vector<SaveMetadata> listSaves();
    bool deleteSave(const std::string& slotName);
    bool renameSave(const std::string& oldName, const std::string& newName);
    bool copySave(const std::string& source, const std::string& dest);

    // Persistent state (survives scene transitions)
    void setPersistent(const std::string& key, const nlohmann::json& value);
    nlohmann::json getPersistent(const std::string& key) const;
    bool hasPersistent(const std::string& key) const;
    void clearPersistent(const std::string& key);

    // Configuration
    void setSaveDirectory(const std::filesystem::path& dir);
    void setAutoSaveInterval(float seconds);
    void enableAutoSave(bool enable);

private:
    SceneManager& _scenes;
    World& _world;

    std::filesystem::path _saveDirectory{"saves"};
    nlohmann::json _persistentState;

    float _autoSaveInterval{300.0f};  // 5 minutes
    float _autoSaveTimer{0.0f};
    bool _autoSaveEnabled{false};

    SaveData buildSaveData();
    void applySaveData(const SaveData& data);
    std::filesystem::path getSavePath(const std::string& slotName);
};
```

### 6.3 Saveable Component Marker

For components that should be saved:

```cpp
// Marker concept
template<typename T>
concept SaveableComponent = ComponentType<T> && requires {
    { T::saveable } -> std::convertible_to<bool>;
};

// Usage
struct PlayerStats : Component<PlayerStats> {
    COMPONENT_NAME("PlayerStats");
    static constexpr bool saveable = true;

    int health{100};
    int maxHealth{100};
    int gold{0};
};

struct ParticleEmitter : Component<ParticleEmitter> {
    COMPONENT_NAME("ParticleEmitter");
    static constexpr bool saveable = false;  // Don't save particle state

    // ...
};
```

---

## Phase 7: Integration with Existing Systems

### 7.1 Application Changes

```cpp
class Application {
public:
    // Existing...

    SceneManager& getSceneManager() { return _sceneManager; }
    SaveManager& getSaveManager() { return _saveManager; }

private:
    // Replace SceneLoader with SceneManager
    SceneManager _sceneManager;
    SaveManager _saveManager;
};
```

### 7.2 World Changes

Add scene awareness:

```cpp
class World {
public:
    // Existing...

    // Scene tracking
    void setEntityScene(EntityId id, SceneHandle scene);
    SceneHandle getEntityScene(EntityId id) const;
    std::vector<EntityId> getEntitiesInScene(SceneHandle scene) const;

private:
    std::unordered_map<EntityId, SceneHandle> _entityScenes;
};
```

### 7.3 Renderer Integration

```cpp
class Renderer2D {
public:
    // Existing...

    // Scene rendering with transitions
    void beginScene(Scene& scene);
    void endScene();
    void renderTransition(SceneTransition& transition, Scene* oldScene, Scene* newScene);

private:
    // Render targets for transition effects
    Surface _sceneABuffer;
    Surface _sceneBBuffer;
};
```

---

## File Structure

```
engine/
├── core/
│   ├── components/
│   │   ├── Transform2D.hpp
│   │   ├── Transform2D.cpp
│   │   └── Hierarchy.hpp           (optional)
│   │
│   ├── systems/
│   │   ├── TransformSystem.hpp
│   │   └── TransformSystem.cpp
│   │
│   ├── scene/
│   │   ├── Scene.hpp
│   │   ├── Scene.cpp
│   │   ├── SceneHandle.hpp
│   │   ├── SceneGraph.hpp
│   │   ├── SceneGraph.cpp
│   │   ├── SceneManager.hpp
│   │   ├── SceneManager.cpp
│   │   ├── SceneSerializer.hpp
│   │   ├── SceneSerializer.cpp
│   │   ├── SceneEvents.hpp
│   │   │
│   │   └── transitions/
│   │       ├── SceneTransition.hpp
│   │       ├── SceneTransition.cpp
│   │       ├── FadeTransition.hpp
│   │       ├── FadeTransition.cpp
│   │       ├── SlideTransition.hpp
│   │       ├── SlideTransition.cpp
│   │       ├── CrossFadeTransition.hpp
│   │       ├── CrossFadeTransition.cpp
│   │       └── ShaderTransition.hpp     (placeholder)
│   │
│   └── save/
│       ├── SaveData.hpp
│       ├── SaveManager.hpp
│       └── SaveManager.cpp
```

---

## Implementation Order

### Sprint 1: Transform Hierarchy (Foundation)
1. Implement `Transform2D` component
2. Implement `TransformSystem` with dirty propagation
3. Add hierarchy manipulation to World (`setParent`, `getChildren`, etc.)
4. Unit tests for transform math and hierarchy

### Sprint 2: Scene Structure
1. Implement `SceneHandle` and `Scene` class
2. Implement `SceneGraph` utility functions
3. Migrate from `SceneLoader` to basic `SceneManager`
4. Update scene JSON format with hierarchy support

### Sprint 3: Scene Manager Features
1. Scene stack for overlays
2. Additive scene loading
3. Scene state machine (Loading, Active, Paused, etc.)
4. Scene events integration with EventBus

### Sprint 4: Scene Transitions
1. `SceneTransition` base class
2. `FadeTransition` implementation
3. `SlideTransition` and `CrossFadeTransition`
4. Renderer integration for transition effects
5. Placeholder for `ShaderTransition`

### Sprint 5: Serialization & Save System
1. Component serialization registration
2. `SceneSerializer` for full scene export/import
3. `SaveManager` basic save/load
4. Persistent state system
5. Auto-save functionality

### Sprint 6: Polish & Integration
1. Integration tests
2. Demo scene with all features
3. Documentation
4. Performance profiling of transform system

---

## Testing Strategy

### Unit Tests
- Transform matrix math (local, world, composition)
- Hierarchy operations (reparent, destroy recursive)
- Scene state machine transitions
- Serialization round-trips

### Integration Tests
- Load scene → modify → save → reload → verify
- Scene transitions with entity state preservation
- Multi-scene additive loading
- Save/load with complex hierarchy

### Performance Tests
- 10,000 entity transform update
- Deep hierarchy (100 levels) propagation
- Rapid scene switching
- Large save file serialization

---

## Open Questions & Future Considerations

1. **Prefab System:** Should prefabs be first-class or just templates? Consider:
   - Prefab overrides
   - Prefab variants
   - Nested prefabs

2. **Async Scene Loading:** Should scenes load on background threads?
   - Need to defer entity creation to main thread
   - Asset loading is already async via AssetManager

3. **Scene Streaming:** For large worlds, consider:
   - Distance-based loading/unloading
   - Scene portals
   - Level of detail for distant scenes

4. **Editor Integration:** Plan for:
   - Scene hierarchy window
   - Entity inspector
   - Drag-and-drop reparenting
   - Undo/redo for hierarchy changes

5. **Networking:** Multi-player scene sync:
   - Entity ownership
   - Transform interpolation
   - Scene authority

---

## Dependencies

**External (already integrated):**
- GLM (transform math)
- nlohmann/json (serialization)
- spdlog (logging)

**Internal prerequisites:**
- Stable ECS (done)
- EventBus (done)
- AssetManager (done)
- Renderer2D basic functionality (in progress)

---

## Success Criteria

- [ ] Can create parent-child entity relationships in code and JSON
- [ ] Child transforms update when parent moves
- [ ] Can load/unload scenes without memory leaks
- [ ] Can stack scenes (main game + pause menu overlay)
- [ ] Fade transition works between scenes
- [ ] Can save game state and restore it exactly
- [ ] 10,000 entities with hierarchy updates at 60 FPS
- [ ] All scene operations emit appropriate events
