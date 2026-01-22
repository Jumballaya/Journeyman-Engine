# Phase 3: Scene Manager Features - Comprehensive Implementation Breakdown

## Overview

Phase 3 enhances the basic SceneManager from Phase 2 with advanced scene management capabilities:
1. **Scene Stack** - Support for stacking scenes (main game + UI overlays, pause menus)
2. **Additive Scene Loading** - Load multiple scenes simultaneously without unloading current
3. **Scene State Management** - Pause, resume, activate, deactivate scenes independently
4. **Scene Events** - Event system integration for scene lifecycle notifications
5. **Scene Manager Update Loop** - Frame-by-frame scene management
6. **Scene Switching** - Switch between scenes (transition support placeholder for Phase 4)

**Goal:** Enable the engine to manage multiple scenes simultaneously, support UI overlays, pause functionality, and provide event-driven scene lifecycle management.

**Success Criteria:**
- ✅ Multiple scenes can be loaded simultaneously
- ✅ Scene stack supports main game + overlay scenes
- ✅ Scenes can be paused/resumed independently
- ✅ Scene events are emitted for all operations
- ✅ SceneManager update loop handles frame-by-frame updates
- ✅ Scene switching works (transitions deferred to Phase 4)
- ✅ No memory leaks with multiple scenes

**Prerequisites:**
- ✅ Phase 1 complete (Transform2D, TransformSystem, SceneGraph utilities)
- ✅ Phase 2 complete (SceneHandle, Scene class, basic SceneManager)
- ✅ EventBus functional
- ✅ World ECS system functional
- ✅ AssetManager functional

---

## Step 3.1: Scene Stack

### Objective
Implement a scene stack system where scenes can be pushed/popped, with the top scene being active. This enables UI overlays, pause menus, and other layered scene scenarios.

### Design Concept

**Scene Stack Behavior:**
- Bottom of stack = main game scene (base layer)
- Top of stack = active scene (overlay, menu, etc.)
- Only top scene is truly "active" (updating)
- Lower scenes can be paused or background
- Popping a scene reveals the scene below

**Example Use Cases:**
- Main game scene + pause menu overlay
- Main game scene + inventory UI overlay
- Main game scene + settings menu overlay
- Multiple UI layers stacked

### Detailed Implementation Steps

#### Step 3.1.1: Add Scene Stack to SceneManager

**File:** `engine/core/scene/SceneManager.hpp` (extend existing from Phase 2)

Add to private members:
```cpp
class SceneManager {
    // ... existing members ...
    
private:
    // ... existing private members ...
    
    /**
     * Scene stack for layered scenes.
     * Bottom (index 0) = main/base scene
     * Top (last index) = active overlay scene
     * Only top scene is active, others are paused or background.
     */
    std::vector<SceneHandle> _sceneStack;
    
    // ... rest of private members ...
};
```

**Action Items:**
- [ ] Add `_sceneStack` vector to SceneManager
- [ ] Update documentation comments
- [ ] Verify compilation

#### Step 3.1.2: Implement pushScene()

**File:** `engine/core/scene/SceneManager.cpp`

Add method:
```cpp
/**
 * Pushes a scene onto the stack, making it active.
 * The previous top scene (if any) is paused.
 * 
 * @param path - Path to scene JSON file
 * @returns SceneHandle for the pushed scene
 */
SceneHandle SceneManager::pushScene(const std::filesystem::path& path) {
    // Load scene (or get existing if already loaded)
    SceneHandle handle;
    
    // Check if scene is already loaded
    bool alreadyLoaded = false;
    for (const auto& [h, scene] : _scenes) {
        if (scene->getName() == path.filename().string()) {
            handle = h;
            alreadyLoaded = true;
            break;
        }
    }
    
    // Load if not already loaded
    if (!alreadyLoaded) {
        handle = loadScene(path);
    }
    
    // Pause previous top scene (if any)
    if (!_sceneStack.empty()) {
        SceneHandle previousTop = _sceneStack.back();
        Scene* previousScene = getScene(previousTop);
        if (previousScene && previousScene->getState() == SceneState::Active) {
            previousScene->onPause();
        }
    }
    
    // Push to stack
    _sceneStack.push_back(handle);
    
    // Activate new top scene
    Scene* scene = getScene(handle);
    if (scene) {
        if (scene->getState() == SceneState::Unloaded) {
            scene->onLoad();
        }
        scene->onActivate();
        
        // Emit event (will be implemented in Step 3.3)
        // _events.emit(events::SceneActivated{handle, scene->getName()});
    }
    
    // Update active scene
    _activeScene = handle;
    
    return handle;
}
```

**Action Items:**
- [ ] Implement `pushScene()` method
- [ ] Handle already-loaded scenes
- [ ] Pause previous top scene
- [ ] Activate new top scene
- [ ] Update `_activeScene`
- [ ] Test pushScene functionality

#### Step 3.1.3: Implement popScene()

**File:** `engine/core/scene/SceneManager.cpp`

Add method:
```cpp
/**
 * Pops the top scene from the stack.
 * The scene below (if any) becomes active.
 * 
 * @returns SceneHandle of the popped scene (invalid if stack was empty)
 */
SceneHandle SceneManager::popScene() {
    if (_sceneStack.empty()) {
        return SceneHandle::invalid();
    }
    
    // Get top scene
    SceneHandle topHandle = _sceneStack.back();
    Scene* topScene = getScene(topHandle);
    
    // Deactivate and pop
    if (topScene) {
        topScene->onDeactivate();
        // Emit event (Step 3.3)
        // _events.emit(events::SceneDeactivated{topHandle, topScene->getName()});
    }
    
    _sceneStack.pop_back();
    
    // Activate new top scene (if any)
    if (!_sceneStack.empty()) {
        SceneHandle newTop = _sceneStack.back();
        Scene* newTopScene = getScene(newTop);
        if (newTopScene) {
            newTopScene->onResume();  // Resume from pause
            // Emit event (Step 3.3)
            // _events.emit(events::SceneActivated{newTop, newTopScene->getName()});
        }
        _activeScene = newTop;
    } else {
        _activeScene = SceneHandle::invalid();
    }
    
    return topHandle;
}
```

**Action Items:**
- [ ] Implement `popScene()` method
- [ ] Handle empty stack case
- [ ] Deactivate popped scene
- [ ] Activate scene below (if any)
- [ ] Update `_activeScene`
- [ ] Test popScene functionality

#### Step 3.1.4: Implement loadSceneAdditive()

**File:** `engine/core/scene/SceneManager.cpp`

Add method:
```cpp
/**
 * Loads a scene additively (without unloading current scenes).
 * Scene is added to the stack and becomes active.
 * 
 * @param path - Path to scene JSON file
 * @returns SceneHandle for the loaded scene
 */
SceneHandle SceneManager::loadSceneAdditive(const std::filesystem::path& path) {
    // Same as pushScene, but ensures scene is loaded
    // (pushScene might reuse existing, loadSceneAdditive always loads)
    return pushScene(path);
}
```

**Alternative Implementation (if you want to distinguish):**
```cpp
SceneHandle SceneManager::loadSceneAdditive(const std::filesystem::path& path) {
    // Always load new scene (don't reuse existing)
    SceneHandle handle = loadScene(path);
    
    // Pause previous top scene
    if (!_sceneStack.empty()) {
        SceneHandle previousTop = _sceneStack.back();
        Scene* previousScene = getScene(previousTop);
        if (previousScene) {
            previousScene->onPause();
        }
    }
    
    // Push to stack
    _sceneStack.push_back(handle);
    
    // Activate
    Scene* scene = getScene(handle);
    if (scene) {
        scene->onActivate();
    }
    
    _activeScene = handle;
    return handle;
}
```

**Action Items:**
- [ ] Implement `loadSceneAdditive()` method
- [ ] Decide on behavior (reuse existing vs always load new)
- [ ] Test additive loading
- [ ] Verify multiple scenes can coexist

#### Step 3.1.5: Update loadScene() to Use Stack

**File:** `engine/core/scene/SceneManager.cpp`

Update existing `loadScene()` method (from Phase 2) to work with stack:
```cpp
SceneHandle SceneManager::loadScene(const std::filesystem::path& path) {
    // ... existing loadScene code ...
    
    // After creating scene:
    // If stack is empty, this becomes the base scene
    if (_sceneStack.empty()) {
        _sceneStack.push_back(handle);
        _activeScene = handle;
    }
    // If stack has scenes, don't automatically push (caller decides)
    // This allows loadScene() to load without activating
    
    return handle;
}
```

**Action Items:**
- [ ] Update `loadScene()` to work with stack
- [ ] Decide: Should loadScene() auto-push to stack or not?
- [ ] Document behavior
- [ ] Test loadScene() behavior

#### Step 3.1.6: Update Active Scene Logic

**File:** `engine/core/scene/SceneManager.cpp`

Update `getActiveScene()` and related methods to use stack:
```cpp
Scene* SceneManager::getActiveScene() {
    if (_sceneStack.empty()) {
        return nullptr;
    }
    SceneHandle topHandle = _sceneStack.back();
    return getScene(topHandle);
}

SceneHandle SceneManager::getActiveSceneHandle() const {
    if (_sceneStack.empty()) {
        return SceneHandle::invalid();
    }
    return _sceneStack.back();
}
```

**Action Items:**
- [ ] Update active scene queries to use stack
- [ ] Verify stack top is always active scene
- [ ] Test active scene queries

#### Step 3.1.7: Test Scene Stack

**Create test file:** `tests/core/scene/SceneManager_stack_test.cpp`

Test cases:
1. Push single scene to empty stack
2. Push multiple scenes (stack grows)
3. Pop scene from stack (reveals scene below)
4. Pop all scenes (stack becomes empty)
5. Active scene is always top of stack
6. Lower scenes are paused when overlay is pushed
7. Lower scenes resume when overlay is popped
8. Multiple overlays (main + pause + inventory)

**Action Items:**
- [ ] Write scene stack tests
- [ ] Run tests and verify all pass
- [ ] Test edge cases (pop from empty, push same scene twice, etc.)

---

## Step 3.2: Scene State Management

### Objective
Implement proper scene state management with pause, resume, activate, and deactivate operations that work correctly with the scene stack.

### State Machine Rules

**Valid State Transitions:**
- Unloaded → Loading → Active
- Active → Paused
- Paused → Active (resume)
- Active → Unloading → Unloaded
- Paused → Unloading → Unloaded

**Invalid Transitions:**
- Unloaded → Paused (must load first)
- Unloaded → Active (must load first)
- Loading → Paused (must complete loading first)

### Detailed Implementation Steps

#### Step 3.2.1: Enhance Scene State Validation

**File:** `engine/core/scene/Scene.cpp` (extend existing)

Add state validation helpers:
```cpp
// In Scene class, add private helper:
private:
    /**
     * Validates that a state transition is allowed.
     * @throws std::runtime_error if transition is invalid
     */
    void validateStateTransition(SceneState from, SceneState to) {
        // Define valid transitions
        static const std::unordered_map<SceneState, std::vector<SceneState>> validTransitions = {
            {SceneState::Unloaded, {SceneState::Loading}},
            {SceneState::Loading, {SceneState::Active, SceneState::Unloaded}},
            {SceneState::Active, {SceneState::Paused, SceneState::Unloading}},
            {SceneState::Paused, {SceneState::Active, SceneState::Unloading}},
            {SceneState::Unloading, {SceneState::Unloaded}}
        };
        
        auto it = validTransitions.find(from);
        if (it == validTransitions.end()) {
            throw std::runtime_error("Invalid source state for transition");
        }
        
        const auto& allowed = it->second;
        if (std::find(allowed.begin(), allowed.end(), to) == allowed.end()) {
            throw std::runtime_error(
                "Invalid state transition from " + stateToString(from) + 
                " to " + stateToString(to)
            );
        }
    }
    
    /**
     * Converts SceneState to string for error messages.
     */
    static std::string stateToString(SceneState state) {
        switch (state) {
            case SceneState::Unloaded: return "Unloaded";
            case SceneState::Loading: return "Loading";
            case SceneState::Active: return "Active";
            case SceneState::Paused: return "Paused";
            case SceneState::Unloading: return "Unloading";
            default: return "Unknown";
        }
    }
```

Update lifecycle methods to validate:
```cpp
void Scene::onLoad() {
    validateStateTransition(_state, SceneState::Loading);
    _state = SceneState::Loading;
    
    // ... rest of onLoad() ...
    
    validateStateTransition(_state, SceneState::Active);
    _state = SceneState::Active;
}

void Scene::onPause() {
    validateStateTransition(_state, SceneState::Paused);
    _state = SceneState::Paused;
}

// Similar for other methods
```

**Action Items:**
- [ ] Add state validation helpers to Scene
- [ ] Update all lifecycle methods to validate transitions
- [ ] Test invalid transitions throw exceptions
- [ ] Test valid transitions work correctly

#### Step 3.2.2: Implement pauseScene() and resumeScene()

**File:** `engine/core/scene/SceneManager.cpp`

Add methods:
```cpp
/**
 * Pauses a scene.
 * Scene must be Active.
 * 
 * @param handle - Scene to pause
 */
void SceneManager::pauseScene(SceneHandle handle) {
    Scene* scene = getScene(handle);
    if (!scene) {
        return;  // Scene not found
    }
    
    if (scene->getState() != SceneState::Active) {
        // Can only pause active scenes
        return;
    }
    
    scene->onPause();
    
    // If this is the active scene, update active scene
    if (_activeScene == handle) {
        // Active scene is paused - this is unusual but allowed
        // (might want to activate another scene)
    }
}

/**
 * Resumes a paused scene.
 * Scene must be Paused.
 * 
 * @param handle - Scene to resume
 */
void SceneManager::resumeScene(SceneHandle handle) {
    Scene* scene = getScene(handle);
    if (!scene) {
        return;
    }
    
    if (scene->getState() != SceneState::Paused) {
        // Can only resume paused scenes
        return;
    }
    
    scene->onResume();
    
    // If resuming the active scene, ensure it's on top of stack
    if (_activeScene == handle) {
        // Already active, just resuming from pause
    }
}
```

**Action Items:**
- [ ] Implement `pauseScene()` method
- [ ] Implement `resumeScene()` method
- [ ] Validate scene state before operations
- [ ] Test pause/resume functionality

#### Step 3.2.3: Implement setActiveScene()

**File:** `engine/core/scene/SceneManager.cpp`

Add method:
```cpp
/**
 * Sets the active scene.
 * Previous active scene is deactivated.
 * New scene is activated and moved to top of stack.
 * 
 * @param handle - Scene to make active
 */
void SceneManager::setActiveScene(SceneHandle handle) {
    Scene* scene = getScene(handle);
    if (!scene) {
        return;  // Scene not found
    }
    
    // Deactivate current active scene
    if (_activeScene.isValid()) {
        Scene* currentActive = getScene(_activeScene);
        if (currentActive && currentActive->getState() == SceneState::Active) {
            currentActive->onDeactivate();
            // Emit event (Step 3.3)
        }
    }
    
    // Ensure scene is loaded
    if (scene->getState() == SceneState::Unloaded) {
        scene->onLoad();
    }
    
    // Activate new scene
    scene->onActivate();
    
    // Move to top of stack (remove from current position if present, add to top)
    auto it = std::find(_sceneStack.begin(), _sceneStack.end(), handle);
    if (it != _sceneStack.end()) {
        _sceneStack.erase(it);
    }
    _sceneStack.push_back(handle);
    
    // Update active scene
    _activeScene = handle;
    
    // Emit event (Step 3.3)
    // _events.emit(events::SceneActivated{handle, scene->getName()});
}
```

**Action Items:**
- [ ] Implement `setActiveScene()` method
- [ ] Handle deactivation of previous active scene
- [ ] Move scene to top of stack
- [ ] Test setActiveScene functionality

#### Step 3.2.4: Update Stack Operations for State Management

**File:** `engine/core/scene/SceneManager.cpp`

Update `pushScene()` and `popScene()` to properly manage states:
```cpp
// In pushScene(), ensure proper state transitions:
// - Previous top: Active → Paused
// - New top: Unloaded/Loading → Active

// In popScene(), ensure proper state transitions:
// - Popped scene: Active → Paused (or Unloading if being destroyed)
// - New top: Paused → Active
```

**Action Items:**
- [ ] Review pushScene() state transitions
- [ ] Review popScene() state transitions
- [ ] Ensure all state changes are valid
- [ ] Test state consistency

#### Step 3.2.5: Test Scene State Management

**Create test file:** `tests/core/scene/SceneManager_state_test.cpp`

Test cases:
1. Pause active scene → state becomes Paused
2. Resume paused scene → state becomes Active
3. setActiveScene() → previous scene deactivated, new scene activated
4. Invalid transitions throw exceptions
5. State consistency across stack operations
6. Multiple scenes with different states

**Action Items:**
- [ ] Write state management tests
- [ ] Run tests and verify all pass
- [ ] Test edge cases

---

## Step 3.3: Scene Events

### Objective
Integrate scene operations with the EventBus system, emitting events for all scene lifecycle operations.

### Event Types

1. **SceneLoadStarted** - When scene loading begins
2. **SceneLoadCompleted** - When scene loading finishes
3. **SceneUnloadStarted** - When scene unloading begins
4. **SceneUnloaded** - When scene unloading completes
5. **SceneActivated** - When scene becomes active
6. **SceneDeactivated** - When scene becomes inactive
7. **SceneTransitionStarted** - When scene transition begins (Phase 4)
8. **SceneTransitionCompleted** - When scene transition completes (Phase 4)

### Detailed Implementation Steps

#### Step 3.3.1: Create Scene Events Header

**File:** `engine/core/scene/SceneEvents.hpp`

```cpp
#pragma once

#include "SceneHandle.hpp"
#include <string>

namespace events {
    /**
     * Emitted when scene loading starts.
     */
    struct SceneLoadStarted {
        SceneHandle handle;
        std::string sceneName;
    };
    
    /**
     * Emitted when scene loading completes.
     */
    struct SceneLoadCompleted {
        SceneHandle handle;
        std::string sceneName;
    };
    
    /**
     * Emitted when scene unloading starts.
     */
    struct SceneUnloadStarted {
        SceneHandle handle;
        std::string sceneName;
    };
    
    /**
     * Emitted when scene unloading completes.
     */
    struct SceneUnloaded {
        SceneHandle handle;
        std::string sceneName;
    };
    
    /**
     * Emitted when a scene becomes active.
     */
    struct SceneActivated {
        SceneHandle handle;
        std::string sceneName;
    };
    
    /**
     * Emitted when a scene becomes inactive.
     */
    struct SceneDeactivated {
        SceneHandle handle;
        std::string sceneName;
    };
    
    /**
     * Emitted when a scene transition starts.
     * (Used in Phase 4 for transitions)
     */
    struct SceneTransitionStarted {
        SceneHandle from;
        SceneHandle to;
        std::string transitionType;  // "fade", "slide", etc.
    };
    
    /**
     * Emitted when a scene transition completes.
     * (Used in Phase 4 for transitions)
     */
    struct SceneTransitionCompleted {
        SceneHandle from;
        SceneHandle to;
    };
}
```

**Action Items:**
- [ ] Create `engine/core/scene/SceneEvents.hpp`
- [ ] Define all scene event structs
- [ ] Add documentation comments
- [ ] Verify compilation

#### Step 3.3.2: Add Events to Event Variant

**File:** `engine/core/events/Event.hpp`

Update Event variant:
```cpp
#pragma once
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <variant>

// Forward declarations
struct SceneHandle;

// Engine Events
namespace events {
struct WindowResized {
  int width{}, height{};
};
struct Quit {};

// Dynamic events to/from scripts
struct DynamicEvent {
  uint32_t id{};
  std::string name;
  nlohmann::json data;
};

// Scene events (forward declared, defined in SceneEvents.hpp)
struct SceneLoadStarted;
struct SceneLoadCompleted;
struct SceneUnloadStarted;
struct SceneUnloaded;
struct SceneActivated;
struct SceneDeactivated;
struct SceneTransitionStarted;
struct SceneTransitionCompleted;
}  // namespace events

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

**Note:** We need to include SceneEvents.hpp in Event.hpp or use forward declarations. Let's include it:

```cpp
#include "SceneEvents.hpp"  // Include scene events
```

**Action Items:**
- [ ] Update Event.hpp to include SceneEvents.hpp
- [ ] Add scene events to Event variant
- [ ] Verify compilation
- [ ] Test event variant works

#### Step 3.3.3: Emit Events from SceneManager

**File:** `engine/core/scene/SceneManager.cpp`

Add event emission throughout SceneManager:

1. **In loadScene():**
```cpp
SceneHandle SceneManager::loadScene(const std::filesystem::path& path) {
    // ... get scene name ...
    
    // Emit load started
    _events.emit(events::SceneLoadStarted{handle, sceneName});
    
    // ... load scene ...
    
    // Emit load completed
    _events.emit(events::SceneLoadCompleted{handle, sceneName});
    
    return handle;
}
```

2. **In unloadScene():**
```cpp
void SceneManager::unloadScene(SceneHandle handle) {
    Scene* scene = getScene(handle);
    if (!scene) return;
    
    std::string sceneName = scene->getName();
    
    // Emit unload started
    _events.emit(events::SceneUnloadStarted{handle, sceneName});
    
    // ... unload scene ...
    
    // Emit unloaded
    _events.emit(events::SceneUnloaded{handle, sceneName});
}
```

3. **In pushScene():**
```cpp
void SceneManager::pushScene(const std::filesystem::path& path) {
    // ... push scene ...
    
    Scene* scene = getScene(handle);
    if (scene) {
        scene->onActivate();
        _events.emit(events::SceneActivated{handle, scene->getName()});
    }
}
```

4. **In popScene():**
```cpp
void SceneManager::popScene() {
    // ... pop scene ...
    
    if (topScene) {
        topScene->onDeactivate();
        _events.emit(events::SceneDeactivated{topHandle, topScene->getName()});
    }
    
    // ... activate new top ...
    if (newTopScene) {
        newTopScene->onResume();
        _events.emit(events::SceneActivated{newTop, newTopScene->getName()});
    }
}
```

5. **In setActiveScene():**
```cpp
void SceneManager::setActiveScene(SceneHandle handle) {
    // ... deactivate current ...
    if (currentActive) {
        currentActive->onDeactivate();
        _events.emit(events::SceneDeactivated{_activeScene, currentActive->getName()});
    }
    
    // ... activate new ...
    scene->onActivate();
    _events.emit(events::SceneActivated{handle, scene->getName()});
}
```

**Action Items:**
- [ ] Add event emission to loadScene()
- [ ] Add event emission to unloadScene()
- [ ] Add event emission to pushScene()
- [ ] Add event emission to popScene()
- [ ] Add event emission to setActiveScene()
- [ ] Add event emission to pauseScene()/resumeScene() if needed
- [ ] Verify all events are emitted at correct times

#### Step 3.3.4: Test Scene Events

**Create test file:** `tests/core/scene/SceneManager_events_test.cpp`

Test cases:
1. LoadScene emits SceneLoadStarted and SceneLoadCompleted
2. UnloadScene emits SceneUnloadStarted and SceneUnloaded
3. PushScene emits SceneActivated
4. PopScene emits SceneDeactivated and SceneActivated (for new top)
5. setActiveScene emits SceneDeactivated and SceneActivated
6. Events contain correct data (handle, sceneName)
7. Event subscribers receive events

**Test Example:**
```cpp
TEST(SceneManager, Events_LoadScene) {
    World world;
    AssetManager assets("test_assets");
    EventBus events;
    SceneManager manager(world, assets, events);
    
    bool loadStartedReceived = false;
    bool loadCompletedReceived = false;
    
    events.subscribe<events::SceneLoadStarted>([&](const events::SceneLoadStarted& e) {
        loadStartedReceived = true;
        EXPECT_TRUE(e.handle.isValid());
        EXPECT_FALSE(e.sceneName.empty());
    });
    
    events.subscribe<events::SceneLoadCompleted>([&](const events::SceneLoadCompleted& e) {
        loadCompletedReceived = true;
        EXPECT_TRUE(e.handle.isValid());
    });
    
    SceneHandle handle = manager.loadScene("test_scene.json");
    events.dispatch();  // Dispatch queued events
    
    EXPECT_TRUE(loadStartedReceived);
    EXPECT_TRUE(loadCompletedReceived);
}
```

**Action Items:**
- [ ] Write event emission tests
- [ ] Run tests and verify events are emitted
- [ ] Test event data correctness
- [ ] Test event subscription works

---

## Step 3.4: Scene Manager Update Loop

### Objective
Implement a frame-by-frame update method for SceneManager to handle any per-frame scene management tasks.

### Update Loop Responsibilities

- Process scene transitions (Phase 4 - placeholder for now)
- Handle deferred scene operations
- Update scene timers if needed
- Process scene state changes

### Detailed Implementation Steps

#### Step 3.4.1: Implement update() Method

**File:** `engine/core/scene/SceneManager.hpp`

Add method declaration:
```cpp
class SceneManager {
    // ... existing methods ...
    
    /**
     * Updates scene manager (call every frame).
     * Handles scene transitions, deferred operations, etc.
     * 
     * @param dt - Delta time since last frame (seconds)
     */
    void update(float dt);
    
    // ... rest of class ...
};
```

**File:** `engine/core/scene/SceneManager.cpp`

Implement method:
```cpp
void SceneManager::update(float dt) {
    (void)dt;  // Not used yet, but will be for transitions
    
    // Process any active scene transitions (Phase 4)
    // if (_activeTransition) {
    //     processTransition(dt);
    // }
    
    // Handle deferred scene operations if needed
    // (e.g., deferred unloads, deferred state changes)
    
    // Update scene timers if needed
    // (e.g., auto-save timers, scene-specific timers)
    
    // For now, this is a placeholder
    // Phase 4 will add transition processing here
}
```

**Action Items:**
- [ ] Add `update()` method declaration
- [ ] Implement basic `update()` method (placeholder)
- [ ] Document future use (transitions in Phase 4)
- [ ] Verify compilation

#### Step 3.4.2: Integrate into Application

**File:** `engine/core/app/Application.cpp`

Update `run()` method:
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

        // Update scene manager (handles transitions, etc.)
        _sceneManager.update(dt);

        _eventBus.dispatch();

        GetModuleRegistry().tickMainThreadModules(*this, dt);
    }

    shutdown();
}
```

**Action Items:**
- [ ] Add `_sceneManager.update(dt)` to Application::run()
- [ ] Verify update is called every frame
- [ ] Test frame-by-frame execution

#### Step 3.4.3: Test Update Loop

**Create test file:** `tests/core/scene/SceneManager_update_test.cpp`

Test cases:
1. Update() can be called every frame
2. Update() doesn't crash with no scenes
3. Update() doesn't crash with multiple scenes
4. Update() performance is acceptable

**Action Items:**
- [ ] Write update loop tests
- [ ] Run tests and verify
- [ ] Performance test (should be very fast, <1ms)

---

## Step 3.5: Scene Switching

### Objective
Implement scene switching functionality that unloads the current scene and loads a new one. Transition support is deferred to Phase 4.

### Design

**switchScene() Behavior:**
- Unloads current active scene(s)
- Loads new scene
- Makes new scene active
- Clears scene stack (or optionally keeps stack)

**Two Variants:**
1. `switchScene(path)` - Simple switch, no transition
2. `switchScene(path, transition)` - Switch with transition (Phase 4)

### Detailed Implementation Steps

#### Step 3.5.1: Implement Basic switchScene()

**File:** `engine/core/scene/SceneManager.hpp`

Add method declaration:
```cpp
class SceneManager {
    // ... existing methods ...
    
    /**
     * Switches to a new scene, unloading the current scene.
     * Simple version without transition.
     * 
     * @param path - Path to new scene JSON file
     * @returns SceneHandle for the new scene
     */
    SceneHandle switchScene(const std::filesystem::path& path);
    
    /**
     * Switches to a new scene with a transition.
     * (Phase 4 - placeholder for now)
     * 
     * @param path - Path to new scene JSON file
     * @param transition - Transition to use (Phase 4)
     */
    void switchScene(const std::filesystem::path& path, /* SceneTransition transition */);
    
    // ... rest of class ...
};
```

**File:** `engine/core/scene/SceneManager.cpp`

Implement basic switchScene():
```cpp
SceneHandle SceneManager::switchScene(const std::filesystem::path& path) {
    // Unload all current scenes
    unloadAllScenes();
    
    // Clear stack
    _sceneStack.clear();
    
    // Load new scene
    SceneHandle handle = loadScene(path);
    
    // Ensure it's on stack and active
    if (_sceneStack.empty()) {
        _sceneStack.push_back(handle);
    }
    _activeScene = handle;
    
    // Activate
    Scene* scene = getScene(handle);
    if (scene && scene->getState() != SceneState::Active) {
        if (scene->getState() == SceneState::Unloaded) {
            scene->onLoad();
        }
        scene->onActivate();
        _events.emit(events::SceneActivated{handle, scene->getName()});
    }
    
    return handle;
}
```

Implement transition version (placeholder):
```cpp
void SceneManager::switchScene(const std::filesystem::path& path, /* SceneTransition transition */) {
    // Phase 4 implementation
    // For now, just call basic switchScene()
    switchScene(path);
    
    // TODO Phase 4:
    // - Store transition
    // - Store from/to scene handles
    // - Start transition
    // - Process in update() loop
}
```

**Action Items:**
- [ ] Implement basic `switchScene()` method
- [ ] Implement transition version (placeholder)
- [ ] Test scene switching
- [ ] Verify old scene is unloaded
- [ ] Verify new scene is loaded and active

#### Step 3.5.2: Add Transition State Members (Placeholder)

**File:** `engine/core/scene/SceneManager.hpp`

Add to private members (for Phase 4):
```cpp
class SceneManager {
    // ... existing members ...
    
private:
    // ... existing private members ...
    
    // Transition state (Phase 4 - placeholder)
    // std::unique_ptr<SceneTransition> _activeTransition;
    // SceneHandle _transitionFrom;
    // SceneHandle _transitionTo;
    
    // Transition helpers (Phase 4)
    // void processTransition(float dt);
    // void finalizeSceneSwitch();
};
```

**Action Items:**
- [ ] Add commented transition state members
- [ ] Document as Phase 4 placeholders
- [ ] Verify compilation

#### Step 3.5.3: Test Scene Switching

**Create test file:** `tests/core/scene/SceneManager_switching_test.cpp`

Test cases:
1. switchScene() unloads current scene
2. switchScene() loads new scene
3. switchScene() makes new scene active
4. switchScene() clears scene stack
5. switchScene() emits appropriate events
6. Multiple switches work correctly

**Action Items:**
- [ ] Write scene switching tests
- [ ] Run tests and verify all pass
- [ ] Test edge cases

---

## Step 3.6: Integration & Comprehensive Testing

### Objective
Ensure all Phase 3 features work together correctly and integrate properly with existing systems.

### Integration Points

#### Step 3.6.1: Verify Stack + State Management Integration

**Test that stack and state management work together:**
- [ ] Pushing scene pauses previous top
- [ ] Popping scene resumes scene below
- [ ] State transitions are valid during stack operations
- [ ] Active scene is always top of stack

#### Step 3.6.2: Verify Events + Operations Integration

**Test that events are emitted for all operations:**
- [ ] All scene operations emit events
- [ ] Event data is correct
- [ ] Events are emitted in correct order
- [ ] Event subscribers work correctly

#### Step 3.6.3: End-to-End Integration Test

**Create comprehensive test:**
```cpp
TEST(SceneManager, EndToEnd_StackAndState) {
    // Setup
    World world;
    AssetManager assets("test_assets");
    EventBus events;
    SceneManager manager(world, assets, events);
    
    // Load main scene
    SceneHandle main = manager.loadScene("main_scene.json");
    
    // Push pause menu
    SceneHandle pause = manager.pushScene("pause_menu.json");
    
    // Verify stack
    EXPECT_EQ(manager.getActiveSceneHandle(), pause);
    EXPECT_EQ(manager.getLoadedScenes().size(), 2);
    
    // Verify states
    Scene* mainScene = manager.getScene(main);
    Scene* pauseScene = manager.getScene(pause);
    EXPECT_EQ(mainScene->getState(), SceneState::Paused);
    EXPECT_EQ(pauseScene->getState(), SceneState::Active);
    
    // Pop pause menu
    manager.popScene();
    
    // Verify main is active again
    EXPECT_EQ(manager.getActiveSceneHandle(), main);
    EXPECT_EQ(mainScene->getState(), SceneState::Active);
    
    // Switch to new scene
    SceneHandle newScene = manager.switchScene("new_scene.json");
    
    // Verify old scenes unloaded
    EXPECT_FALSE(manager.getScene(main));
    EXPECT_FALSE(manager.getScene(pause));
    EXPECT_TRUE(manager.getScene(newScene));
}
```

**Action Items:**
- [ ] Write end-to-end integration test
- [ ] Run test and verify all functionality
- [ ] Fix any integration issues

#### Step 3.6.4: Memory Leak Testing

**Test for memory leaks:**
- [ ] Load/unload scenes multiple times
- [ ] Push/pop scenes multiple times
- [ ] Switch scenes multiple times
- [ ] Verify no memory leaks with valgrind or similar

**Action Items:**
- [ ] Run memory leak tests
- [ ] Fix any leaks found
- [ ] Verify cleanup is complete

---

## Phase 3 Completion Checklist

### Functional Requirements
- [ ] Scene stack works (push/pop)
- [ ] Additive scene loading works
- [ ] Multiple scenes can coexist
- [ ] Scene state management works (pause/resume/activate/deactivate)
- [ ] State transitions are validated
- [ ] Scene events are emitted for all operations
- [ ] SceneManager update loop runs every frame
- [ ] Scene switching works
- [ ] Active scene is always top of stack
- [ ] Lower scenes are paused when overlay is pushed

### Code Quality
- [ ] Code compiles without warnings
- [ ] All functions documented
- [ ] Error handling implemented
- [ ] State validation works
- [ ] Memory safety verified (no leaks)
- [ ] Code follows project conventions

### Integration
- [ ] SceneManager integrates with Application
- [ ] Events integrate with EventBus
- [ ] Works with Phase 1 and Phase 2 components
- [ ] Ready for Phase 4 (transitions - placeholders in place)

### Testing
- [ ] Scene stack tests pass
- [ ] State management tests pass
- [ ] Event emission tests pass
- [ ] Update loop tests pass
- [ ] Scene switching tests pass
- [ ] Integration tests pass
- [ ] Memory leak tests pass

---

## Estimated Time Breakdown

- **Step 3.1 (Scene Stack):** 6-8 hours
- **Step 3.2 (State Management):** 5-6 hours
- **Step 3.3 (Scene Events):** 4-5 hours
- **Step 3.4 (Update Loop):** 2-3 hours
- **Step 3.5 (Scene Switching):** 3-4 hours
- **Step 3.6 (Integration & Testing):** 5-7 hours

**Total Phase 3:** 25-33 hours

---

## Common Pitfalls & Solutions

### Pitfall 1: Stack Inconsistency
**Issue:** Active scene not matching top of stack
**Solution:** Always update `_activeScene` when modifying stack, validate in getActiveScene()

### Pitfall 2: State Transition Errors
**Issue:** Invalid state transitions cause crashes
**Solution:** Validate all state transitions, throw exceptions for invalid operations

### Pitfall 3: Event Emission Timing
**Issue:** Events emitted before state actually changes
**Solution:** Emit events after state changes are complete

### Pitfall 4: Stack Empty After Pop
**Issue:** Popping last scene leaves no active scene
**Solution:** Handle empty stack case, set `_activeScene` to invalid

### Pitfall 5: Memory Leaks with Stack
**Issue:** Scenes in stack not unloaded when manager destroyed
**Solution:** Destructor should unload all scenes, clear stack

---

## Dependencies on Previous Phases

Phase 3 requires:
- ✅ Phase 1: Transform2D, TransformSystem, SceneGraph utilities
- ✅ Phase 2: SceneHandle, Scene class, basic SceneManager

If Phase 1 or Phase 2 are not complete, Phase 3 cannot proceed.

---

## Next Steps After Phase 3

Once Phase 3 is complete:
1. ✅ Advanced scene management is in place
2. ✅ Scene stack supports UI overlays
3. ✅ Event system integrated
4. ✅ Ready for Phase 4 (scene transitions)
5. ✅ Ready for Phase 5 (serialization)

**Phase 3 establishes advanced scene management - ensure it's robust before moving to Phase 4!**
