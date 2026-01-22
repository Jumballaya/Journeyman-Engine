# Phase 4: Scene Transitions - Comprehensive Implementation Breakdown

## Overview

Phase 4 implements visual transitions between scenes, providing smooth, professional scene switching effects. This phase includes:
1. **SceneTransition Base Class** - Abstract base for all transition types
2. **Built-in Transitions** - FadeTransition, SlideTransition, CrossFadeTransition
3. **SceneManager Integration** - Use transitions when switching scenes
4. **Renderer Integration** - Render scenes to textures and composite with transitions
5. **ShaderTransition Placeholder** - Interface for future shader-based transitions

**Goal:** Enable smooth, visually appealing transitions when switching between scenes, with support for multiple transition types and extensibility for custom transitions.

**Success Criteria:**
- ✅ Fade transition works between scenes
- ✅ Slide transition works in all directions
- ✅ Crossfade transition works
- ✅ Transitions complete correctly and scenes switch
- ✅ Transition events are emitted
- ✅ Renderer can render scenes to textures for transitions
- ✅ No visual glitches or artifacts during transitions

**Prerequisites:**
- ✅ Phase 1 complete (Transform2D, TransformSystem)
- ✅ Phase 2 complete (Scene, SceneManager basic)
- ✅ Phase 3 complete (SceneManager with stack, events)
- ✅ Renderer2D functional with Surface support
- ✅ EventBus functional

---

## Step 4.1: SceneTransition Base Class

### Objective
Create an abstract base class that defines the interface and common behavior for all scene transitions.

### Design Concept

**Transition Lifecycle:**
1. **Out Phase** - Old scene fades/slides out (progress 0.0 → 1.0)
2. **In Phase** - New scene fades/slides in (progress 0.0 → 1.0)
3. **Complete** - Transition finished, scenes switched

**Base Class Responsibilities:**
- Track elapsed time and progress
- Manage phase transitions (Out → In)
- Provide virtual render() method for subclasses
- Notify when phases complete

### Detailed Implementation Steps

#### Step 4.1.1: Create Transition Base Class Header

**File:** `engine/core/scene/transitions/SceneTransition.hpp`

1. **Include necessary headers:**
   ```cpp
   #pragma once
   
   #include <memory>
   
   // Forward declaration
   class Renderer2D;
   ```

2. **Define TransitionPhase enum:**
   ```cpp
   /**
    * Represents the current phase of a scene transition.
    */
   enum class TransitionPhase {
       Out,    // Fading out old scene
       In      // Fading in new scene
   };
   ```

3. **Declare SceneTransition class:**
   ```cpp
   /**
    * Base class for all scene transitions.
    * Manages transition timing, phases, and provides rendering interface.
    */
   class SceneTransition {
   public:
       /**
        * Creates a transition with the specified duration.
        * @param duration - Total duration in seconds (Out phase + In phase)
        */
       explicit SceneTransition(float duration);
       
       /**
        * Virtual destructor for proper cleanup.
        */
       virtual ~SceneTransition() = default;
       
       // Non-copyable
       SceneTransition(const SceneTransition&) = delete;
       SceneTransition& operator=(const SceneTransition&) = delete;
       
       // Movable (if needed)
       SceneTransition(SceneTransition&&) noexcept = default;
       SceneTransition& operator=(SceneTransition&&) noexcept = default;
       
       // ====================================================================
       // Update & State
       // ====================================================================
       
       /**
        * Updates the transition (call every frame).
        * @param dt - Delta time since last frame (seconds)
        */
       void update(float dt);
       
       /**
        * Checks if the transition is complete.
        */
       bool isComplete() const {
           return _complete;
       }
       
       /**
        * Gets the current progress within the current phase (0.0 to 1.0).
        */
       float getProgress() const {
           return _progress;
       }
       
       /**
        * Gets the current phase (Out or In).
        */
       TransitionPhase getPhase() const {
           return _phase;
       }
       
       /**
        * Gets the total duration of the transition.
        */
       float getDuration() const {
           return _duration;
       }
       
       /**
        * Gets the elapsed time since transition started.
        */
       float getElapsed() const {
           return _elapsed;
       }
       
       // ====================================================================
       // Rendering
       // ====================================================================
       
       /**
        * Renders the transition effect.
        * Subclasses override this to implement specific transition effects.
        * 
        * @param renderer - Renderer2D instance
        * @param oldSceneTexture - Texture of the old scene (rendered to surface)
        * @param newSceneTexture - Texture of the new scene (rendered to surface)
        * @param screenWidth - Screen width in pixels
        * @param screenHeight - Screen height in pixels
        */
       virtual void render(
           Renderer2D& renderer,
           const gl::Texture2D& oldSceneTexture,
           const gl::Texture2D& newSceneTexture,
           int screenWidth,
           int screenHeight
       ) = 0;
       
   protected:
       /**
        * Called when a phase completes.
        * Subclasses can override to perform phase-specific logic.
        * 
        * @param completed - The phase that just completed
        */
       virtual void onPhaseComplete(TransitionPhase completed) {
           (void)completed;  // Default: do nothing
       }
       
       float _duration;              // Total duration (seconds)
       float _elapsed{0.0f};         // Elapsed time (seconds)
       float _progress{0.0f};         // Progress in current phase [0.0, 1.0]
       TransitionPhase _phase{TransitionPhase::Out};  // Current phase
       bool _complete{false};        // True when transition is complete
   };
   ```

**Action Items:**
- [ ] Create `engine/core/scene/transitions/SceneTransition.hpp`
- [ ] Define TransitionPhase enum
- [ ] Declare SceneTransition class
- [ ] Add forward declarations
- [ ] Verify compilation

#### Step 4.1.2: Implement Transition Base Class

**File:** `engine/core/scene/transitions/SceneTransition.cpp`

1. **Include headers:**
   ```cpp
   #include "SceneTransition.hpp"
   #include "../../renderer2d/Renderer2D.hpp"
   ```

2. **Implement constructor:**
   ```cpp
   SceneTransition::SceneTransition(float duration)
       : _duration(duration > 0.0f ? duration : 0.1f),  // Minimum 0.1s
         _elapsed(0.0f),
         _progress(0.0f),
         _phase(TransitionPhase::Out),
         _complete(false) {
   }
   ```

3. **Implement update() method:**
   ```cpp
   void SceneTransition::update(float dt) {
       if (_complete) {
           return;  // Already complete
       }
       
       _elapsed += dt;
       
       // Calculate phase duration (half of total for each phase)
       float phaseDuration = _duration / 2.0f;
       
       if (_phase == TransitionPhase::Out) {
           // Out phase: fade out old scene
           if (_elapsed >= phaseDuration) {
               // Out phase complete
               _progress = 1.0f;
               onPhaseComplete(TransitionPhase::Out);
               
               // Switch to In phase
               _phase = TransitionPhase::In;
               _elapsed = 0.0f;  // Reset elapsed for In phase
           } else {
               // Calculate progress in Out phase
               _progress = _elapsed / phaseDuration;
           }
       } else {
           // In phase: fade in new scene
           if (_elapsed >= phaseDuration) {
               // In phase complete - transition done
               _progress = 1.0f;
               onPhaseComplete(TransitionPhase::In);
               _complete = true;
           } else {
               // Calculate progress in In phase
               _progress = _elapsed / phaseDuration;
           }
       }
   }
   ```

**Action Items:**
- [ ] Create `engine/core/scene/transitions/SceneTransition.cpp`
- [ ] Implement constructor
- [ ] Implement update() method
- [ ] Test phase transitions
- [ ] Verify progress calculation

#### Step 4.1.3: Test Base Transition

**Create test file:** `tests/core/scene/transitions/SceneTransition_test.cpp`

Test cases:
1. Constructor sets correct initial values
2. update() progresses through Out phase
3. update() transitions from Out to In phase
4. update() completes transition
5. isComplete() returns correct value
6. getProgress() returns correct values [0.0, 1.0]
7. getPhase() returns correct phase

**Action Items:**
- [ ] Write base transition tests
- [ ] Run tests and verify all pass
- [ ] Test edge cases (very short duration, very long duration)

---

## Step 4.2: FadeTransition

### Objective
Implement a simple fade transition that fades the old scene to black (or specified color) and then fades in the new scene.

### Design

**FadeTransition Behavior:**
- Out phase: Old scene fades to solid color (alpha 1.0 → 0.0, overlay color alpha 0.0 → 1.0)
- In phase: New scene fades in from solid color (overlay color alpha 1.0 → 0.0, new scene alpha 0.0 → 1.0)

### Detailed Implementation Steps

#### Step 4.2.1: Create FadeTransition Header

**File:** `engine/core/scene/transitions/FadeTransition.hpp`

```cpp
#pragma once

#include "SceneTransition.hpp"
#include <glm/glm.hpp>

/**
 * Fade transition fades the old scene to a solid color, then fades in the new scene.
 */
class FadeTransition : public SceneTransition {
public:
    /**
     * Creates a fade transition.
     * @param duration - Total duration in seconds
     * @param color - Color to fade to (default: black)
     */
    FadeTransition(float duration, glm::vec4 color = {0.0f, 0.0f, 0.0f, 1.0f});
    
    /**
     * Renders the fade transition effect.
     */
    void render(
        Renderer2D& renderer,
        const gl::Texture2D& oldSceneTexture,
        const gl::Texture2D& newSceneTexture,
        int screenWidth,
        int screenHeight
    ) override;
    
private:
    glm::vec4 _color;  // Fade color (RGBA)
};
```

**Action Items:**
- [ ] Create `engine/core/scene/transitions/FadeTransition.hpp`
- [ ] Declare FadeTransition class
- [ ] Verify compilation

#### Step 4.2.2: Implement FadeTransition

**File:** `engine/core/scene/transitions/FadeTransition.cpp`

1. **Include headers:**
   ```cpp
   #include "FadeTransition.hpp"
   #include "../../renderer2d/Renderer2D.hpp"
   #include "../../renderer2d/gl/Texture2D.hpp"
   #include "../../renderer2d/gl/Shader.hpp"
   #include <glm/glm.hpp>
   ```

2. **Implement constructor:**
   ```cpp
   FadeTransition::FadeTransition(float duration, glm::vec4 color)
       : SceneTransition(duration), _color(color) {
   }
   ```

3. **Implement render() method:**
   ```cpp
   void FadeTransition::render(
       Renderer2D& renderer,
       const gl::Texture2D& oldSceneTexture,
       const gl::Texture2D& newSceneTexture,
       int screenWidth,
       int screenHeight
   ) {
       // For fade transition:
       // Out phase: Draw old scene with decreasing alpha, overlay with increasing alpha
       // In phase: Draw new scene with increasing alpha, overlay with decreasing alpha
       
       // Calculate alpha values based on phase and progress
       float oldSceneAlpha = 1.0f;
       float newSceneAlpha = 0.0f;
       float overlayAlpha = 0.0f;
       
       if (_phase == TransitionPhase::Out) {
           // Fading out old scene
           oldSceneAlpha = 1.0f - _progress;  // 1.0 → 0.0
           overlayAlpha = _progress;          // 0.0 → 1.0
       } else {
           // Fading in new scene
           oldSceneAlpha = 0.0f;
           newSceneAlpha = _progress;         // 0.0 → 1.0
           overlayAlpha = 1.0f - _progress;    // 1.0 → 0.0
       }
       
       // Render old scene (if visible)
       if (oldSceneAlpha > 0.0f) {
           // TODO: Render old scene texture with alpha
           // This requires renderer support for rendering textures with alpha
           // For now, placeholder
       }
       
       // Render new scene (if visible)
       if (newSceneAlpha > 0.0f) {
           // TODO: Render new scene texture with alpha
       }
       
       // Render overlay color
       if (overlayAlpha > 0.0f) {
           // TODO: Render fullscreen quad with fade color and alpha
           // This requires a simple fullscreen quad renderer
       }
   }
   ```

**Note:** The actual rendering implementation depends on Renderer2D capabilities. We'll need to add methods to render textures and fullscreen quads. Let's create a placeholder that can be enhanced.

**Action Items:**
- [ ] Create `engine/core/scene/transitions/FadeTransition.cpp`
- [ ] Implement constructor
- [ ] Implement basic render() method (placeholder for now)
- [ ] Document what renderer methods are needed
- [ ] Verify compilation

#### Step 4.2.3: Add Renderer Support for Transitions

**File:** `engine/renderer2d/Renderer2D.hpp`

Add methods needed for transitions:
```cpp
class Renderer2D {
    // ... existing methods ...
    
    /**
     * Renders a fullscreen texture with alpha blending.
     * Used for scene transitions.
     * 
     * @param texture - Texture to render
     * @param alpha - Alpha value [0.0, 1.0]
     */
    void drawFullscreenTexture(const gl::Texture2D& texture, float alpha);
    
    /**
     * Renders a fullscreen colored quad.
     * Used for fade transitions.
     * 
     * @param color - RGBA color
     */
    void drawFullscreenQuad(const glm::vec4& color);
    
    // ... rest of class ...
};
```

**File:** `engine/renderer2d/Renderer2D.cpp` (if exists, or implement in header)

Implement methods:
```cpp
void Renderer2D::drawFullscreenTexture(const gl::Texture2D& texture, float alpha) {
    // Use screen shader to render texture fullscreen
    // Set alpha uniform
    // Render quad covering entire screen
    
    // Implementation details:
    // 1. Bind screen shader
    // 2. Set texture
    // 3. Set alpha uniform
    // 4. Render fullscreen quad (or use existing blit with alpha)
    
    // For now, this is a placeholder that will need proper implementation
    // based on your shader system
}

void Renderer2D::drawFullscreenQuad(const glm::vec4& color) {
    // Render a fullscreen quad with the specified color
    // Can use a simple shader or add to existing screen shader
    
    // Implementation placeholder
}
```

**Action Items:**
- [ ] Add renderer methods for fullscreen rendering
- [ ] Implement basic versions (can be enhanced later)
- [ ] Test fullscreen rendering works
- [ ] Document shader requirements

#### Step 4.2.4: Complete FadeTransition Implementation

**File:** `engine/core/scene/transitions/FadeTransition.cpp`

Update render() with actual implementation:
```cpp
void FadeTransition::render(
    Renderer2D& renderer,
    const gl::Texture2D& oldSceneTexture,
    const gl::Texture2D& newSceneTexture,
    int screenWidth,
    int screenHeight
) {
    // Calculate alpha values
    float oldSceneAlpha = 1.0f;
    float newSceneAlpha = 0.0f;
    float overlayAlpha = 0.0f;
    
    if (_phase == TransitionPhase::Out) {
        oldSceneAlpha = 1.0f - _progress;
        overlayAlpha = _progress;
    } else {
        oldSceneAlpha = 0.0f;
        newSceneAlpha = _progress;
        overlayAlpha = 1.0f - _progress;
    }
    
    // Render old scene (if visible)
    if (oldSceneAlpha > 0.001f) {  // Small threshold to avoid unnecessary draws
        renderer.drawFullscreenTexture(oldSceneTexture, oldSceneAlpha);
    }
    
    // Render new scene (if visible)
    if (newSceneAlpha > 0.001f) {
        renderer.drawFullscreenTexture(newSceneTexture, newSceneAlpha);
    }
    
    // Render overlay color
    if (overlayAlpha > 0.001f) {
        glm::vec4 overlayColor = _color;
        overlayColor.a *= overlayAlpha;  // Apply alpha to color
        renderer.drawFullscreenQuad(overlayColor);
    }
}
```

**Action Items:**
- [ ] Complete FadeTransition::render() implementation
- [ ] Test fade transition rendering
- [ ] Verify alpha blending works correctly
- [ ] Test with different fade colors

#### Step 4.2.5: Test FadeTransition

**Create test file:** `tests/core/scene/transitions/FadeTransition_test.cpp`

Test cases:
1. FadeTransition creates with correct color
2. Out phase fades old scene correctly
3. In phase fades new scene correctly
4. Transition completes correctly
5. Different fade colors work
6. Very short duration works
7. Very long duration works

**Action Items:**
- [ ] Write FadeTransition tests
- [ ] Run tests and verify all pass
- [ ] Visual test: Actually see fade transition in action

---

## Step 4.3: SlideTransition

### Objective
Implement a slide transition where the old scene slides out in one direction while the new scene slides in from the opposite direction.

### Design

**SlideTransition Behavior:**
- Out phase: Old scene slides out (moves off-screen)
- In phase: New scene slides in (moves from off-screen to on-screen)
- Direction: Left, Right, Up, or Down

### Detailed Implementation Steps

#### Step 4.3.1: Create SlideTransition Header

**File:** `engine/core/scene/transitions/SlideTransition.hpp`

```cpp
#pragma once

#include "SceneTransition.hpp"
#include <glm/glm.hpp>

/**
 * Direction for slide transitions.
 */
enum class SlideDirection {
    Left,   // Old scene slides left, new from right
    Right,  // Old scene slides right, new from left
    Up,     // Old scene slides up, new from down
    Down    // Old scene slides down, new from up
};

/**
 * Slide transition slides the old scene out while sliding the new scene in.
 */
class SlideTransition : public SceneTransition {
public:
    /**
     * Creates a slide transition.
     * @param duration - Total duration in seconds
     * @param direction - Direction of slide
     */
    SlideTransition(float duration, SlideDirection direction);
    
    /**
     * Renders the slide transition effect.
     */
    void render(
        Renderer2D& renderer,
        const gl::Texture2D& oldSceneTexture,
        const gl::Texture2D& newSceneTexture,
        int screenWidth,
        int screenHeight
    ) override;
    
    /**
     * Gets the offset for the old scene (for Out phase).
     * @param screenWidth - Screen width
     * @param screenHeight - Screen height
     * @returns Offset vector (x, y)
     */
    glm::vec2 getOldSceneOffset(int screenWidth, int screenHeight) const;
    
    /**
     * Gets the offset for the new scene (for In phase).
     * @param screenWidth - Screen width
     * @param screenHeight - Screen height
     * @returns Offset vector (x, y)
     */
    glm::vec2 getNewSceneOffset(int screenWidth, int screenHeight) const;
    
private:
    SlideDirection _direction;
};
```

**Action Items:**
- [ ] Create `engine/core/scene/transitions/SlideTransition.hpp`
- [ ] Define SlideDirection enum
- [ ] Declare SlideTransition class
- [ ] Verify compilation

#### Step 4.3.2: Implement SlideTransition

**File:** `engine/core/scene/transitions/SlideTransition.cpp`

1. **Include headers:**
   ```cpp
   #include "SlideTransition.hpp"
   #include "../../renderer2d/Renderer2D.hpp"
   #include <glm/glm.hpp>
   ```

2. **Implement constructor:**
   ```cpp
   SlideTransition::SlideTransition(float duration, SlideDirection direction)
       : SceneTransition(duration), _direction(direction) {
   }
   ```

3. **Implement getOldSceneOffset():**
   ```cpp
   glm::vec2 SlideTransition::getOldSceneOffset(int screenWidth, int screenHeight) const {
       glm::vec2 offset{0.0f, 0.0f};
       
       if (_phase != TransitionPhase::Out) {
           return offset;  // Only applies during Out phase
       }
       
       // Calculate offset based on direction and progress
       float distance = static_cast<float>(_direction == SlideDirection::Left || 
                                          _direction == SlideDirection::Right 
                                          ? screenWidth : screenHeight);
       float currentOffset = distance * _progress;
       
       switch (_direction) {
           case SlideDirection::Left:
               offset.x = -currentOffset;  // Slide left (negative X)
               break;
           case SlideDirection::Right:
               offset.x = currentOffset;   // Slide right (positive X)
               break;
           case SlideDirection::Up:
               offset.y = currentOffset;    // Slide up (positive Y in screen coords)
               break;
           case SlideDirection::Down:
               offset.y = -currentOffset;   // Slide down (negative Y)
               break;
       }
       
       return offset;
   }
   ```

4. **Implement getNewSceneOffset():**
   ```cpp
   glm::vec2 SlideTransition::getNewSceneOffset(int screenWidth, int screenHeight) const {
       glm::vec2 offset{0.0f, 0.0f};
       
       if (_phase != TransitionPhase::In) {
           // During Out phase, new scene is off-screen
           float distance = static_cast<float>(_direction == SlideDirection::Left || 
                                              _direction == SlideDirection::Right 
                                              ? screenWidth : screenHeight);
           
           switch (_direction) {
               case SlideDirection::Left:
                   offset.x = distance;   // Start from right
                   break;
               case SlideDirection::Right:
                   offset.x = -distance; // Start from left
                   break;
               case SlideDirection::Up:
                   offset.y = -distance;  // Start from bottom
                   break;
               case SlideDirection::Down:
                   offset.y = distance;   // Start from top
                   break;
           }
           return offset;
       }
       
       // During In phase, slide in
       float distance = static_cast<float>(_direction == SlideDirection::Left || 
                                          _direction == SlideDirection::Right 
                                          ? screenWidth : screenHeight);
       float currentOffset = distance * (1.0f - _progress);  // 1.0 → 0.0
       
       switch (_direction) {
           case SlideDirection::Left:
               offset.x = currentOffset;   // Slide in from right
               break;
           case SlideDirection::Right:
               offset.x = -currentOffset;  // Slide in from left
               break;
           case SlideDirection::Up:
               offset.y = -currentOffset; // Slide in from bottom
               break;
           case SlideDirection::Down:
               offset.y = currentOffset;   // Slide in from top
               break;
       }
       
       return offset;
   }
   ```

5. **Implement render() method:**
   ```cpp
   void SlideTransition::render(
       Renderer2D& renderer,
       const gl::Texture2D& oldSceneTexture,
       const gl::Texture2D& newSceneTexture,
       int screenWidth,
       int screenHeight
   ) {
       // Get offsets for both scenes
       glm::vec2 oldOffset = getOldSceneOffset(screenWidth, screenHeight);
       glm::vec2 newOffset = getNewSceneOffset(screenWidth, screenHeight);
       
       // Render old scene with offset (if visible)
       if (_phase == TransitionPhase::Out || _progress < 1.0f) {
           // TODO: Render old scene texture with offset
           // Requires renderer support for rendering textures with position offset
           renderer.drawFullscreenTexture(oldSceneTexture, 1.0f, oldOffset);
       }
       
       // Render new scene with offset (if visible)
       if (_phase == TransitionPhase::In || _progress > 0.0f) {
           // TODO: Render new scene texture with offset
           renderer.drawFullscreenTexture(newSceneTexture, 1.0f, newOffset);
       }
   }
   ```

**Note:** The renderer will need to support offset rendering. We'll need to add that capability.

**Action Items:**
- [ ] Create `engine/core/scene/transitions/SlideTransition.cpp`
- [ ] Implement all methods
- [ ] Add renderer support for offset rendering (if needed)
- [ ] Test slide transition in all directions

#### Step 4.3.3: Enhance Renderer for Offset Rendering

**File:** `engine/renderer2d/Renderer2D.hpp`

Add method:
```cpp
class Renderer2D {
    // ... existing methods ...
    
    /**
     * Renders a fullscreen texture with offset and alpha.
     * Used for slide transitions.
     * 
     * @param texture - Texture to render
     * @param alpha - Alpha value [0.0, 1.0]
     * @param offset - Position offset (x, y) in pixels
     */
    void drawFullscreenTexture(const gl::Texture2D& texture, float alpha, const glm::vec2& offset);
    
    // ... rest of class ...
};
```

**Action Items:**
- [ ] Add offset rendering support to renderer
- [ ] Implement drawFullscreenTexture with offset
- [ ] Test offset rendering works

#### Step 4.3.4: Test SlideTransition

**Create test file:** `tests/core/scene/transitions/SlideTransition_test.cpp`

Test cases:
1. SlideTransition creates with correct direction
2. getOldSceneOffset() returns correct offset for Out phase
3. getNewSceneOffset() returns correct offset for In phase
4. All four directions work (Left, Right, Up, Down)
5. Offset calculations are correct at progress 0.0, 0.5, 1.0
6. Transition completes correctly

**Action Items:**
- [ ] Write SlideTransition tests
- [ ] Run tests and verify all pass
- [ ] Visual test: See slide transitions in all directions

---

## Step 4.4: CrossFadeTransition

### Objective
Implement a crossfade transition where the old scene fades out while the new scene simultaneously fades in (overlapping fade).

### Design

**CrossFadeTransition Behavior:**
- Both scenes are visible simultaneously
- Old scene alpha: 1.0 → 0.0 (over entire transition)
- New scene alpha: 0.0 → 1.0 (over entire transition)
- No separate Out/In phases - single continuous fade

### Detailed Implementation Steps

#### Step 4.4.1: Create CrossFadeTransition Header

**File:** `engine/core/scene/transitions/CrossFadeTransition.hpp`

```cpp
#pragma once

#include "SceneTransition.hpp"

/**
 * Crossfade transition fades out the old scene while fading in the new scene simultaneously.
 * Both scenes are visible during the transition.
 */
class CrossFadeTransition : public SceneTransition {
public:
    /**
     * Creates a crossfade transition.
     * @param duration - Total duration in seconds
     */
    explicit CrossFadeTransition(float duration);
    
    /**
     * Renders the crossfade transition effect.
     */
    void render(
        Renderer2D& renderer,
        const gl::Texture2D& oldSceneTexture,
        const gl::Texture2D& newSceneTexture,
        int screenWidth,
        int screenHeight
    ) override;
    
    /**
     * Gets the alpha value for the old scene.
     * @returns Alpha [0.0, 1.0]
     */
    float getOldSceneAlpha() const;
    
    /**
     * Gets the alpha value for the new scene.
     * @returns Alpha [0.0, 1.0]
     */
    float getNewSceneAlpha() const;
};
```

**Action Items:**
- [ ] Create `engine/core/scene/transitions/CrossFadeTransition.hpp`
- [ ] Declare CrossFadeTransition class
- [ ] Verify compilation

#### Step 4.4.2: Implement CrossFadeTransition

**File:** `engine/core/scene/transitions/CrossFadeTransition.cpp`

1. **Include headers:**
   ```cpp
   #include "CrossFadeTransition.hpp"
   #include "../../renderer2d/Renderer2D.hpp"
   ```

2. **Override update() to use single phase:**
   ```cpp
   // Note: We might want to override update() to not use Out/In phases
   // For crossfade, we want a single continuous fade
   // But we can use the base class and just ignore phases
   ```

3. **Implement constructor:**
   ```cpp
   CrossFadeTransition::CrossFadeTransition(float duration)
       : SceneTransition(duration) {
       // Crossfade uses the full duration as a single phase
       // We'll handle this in getOldSceneAlpha/getNewSceneAlpha
   }
   ```

4. **Implement getOldSceneAlpha():**
   ```cpp
   float CrossFadeTransition::getOldSceneAlpha() const {
       // Old scene fades out over entire duration
       // Use total elapsed time, not phase progress
       float totalProgress = _elapsed / _duration;
       if (totalProgress > 1.0f) totalProgress = 1.0f;
       return 1.0f - totalProgress;  // 1.0 → 0.0
   }
   ```

5. **Implement getNewSceneAlpha():**
   ```cpp
   float CrossFadeTransition::getNewSceneAlpha() const {
       // New scene fades in over entire duration
       float totalProgress = _elapsed / _duration;
       if (totalProgress > 1.0f) totalProgress = 1.0f;
       return totalProgress;  // 0.0 → 1.0
   }
   ```

6. **Implement render() method:**
   ```cpp
   void CrossFadeTransition::render(
       Renderer2D& renderer,
       const gl::Texture2D& oldSceneTexture,
       const gl::Texture2D& newSceneTexture,
       int screenWidth,
       int screenHeight
   ) {
       float oldAlpha = getOldSceneAlpha();
       float newAlpha = getNewSceneAlpha();
       
       // Render old scene (if visible)
       if (oldAlpha > 0.001f) {
           renderer.drawFullscreenTexture(oldSceneTexture, oldAlpha);
       }
       
       // Render new scene (if visible) - blends with old scene
       if (newAlpha > 0.001f) {
           renderer.drawFullscreenTexture(newSceneTexture, newAlpha);
       }
   }
   ```

**Note:** Crossfade might need to override update() to not use the two-phase system, or we can work with it. Let's override update() to use a single phase.

**Alternative: Override update() for single-phase:**
```cpp
// In CrossFadeTransition, we can override update() to use single phase
void CrossFadeTransition::update(float dt) {
    if (_complete) return;
    
    _elapsed += dt;
    
    // Single continuous fade over full duration
    _progress = _elapsed / _duration;
    if (_progress > 1.0f) {
        _progress = 1.0f;
        _complete = true;
    }
    
    // Always in "In" phase for simplicity (doesn't matter for crossfade)
    _phase = TransitionPhase::In;
}
```

**Action Items:**
- [ ] Create `engine/core/scene/transitions/CrossFadeTransition.cpp`
- [ ] Implement all methods
- [ ] Override update() for single-phase behavior (optional)
- [ ] Test crossfade transition

#### Step 4.4.3: Test CrossFadeTransition

**Create test file:** `tests/core/scene/transitions/CrossFadeTransition_test.cpp`

Test cases:
1. CrossFadeTransition creates correctly
2. getOldSceneAlpha() decreases from 1.0 to 0.0
3. getNewSceneAlpha() increases from 0.0 to 1.0
4. Both alphas are correct at various progress points
5. Transition completes correctly
6. Both scenes visible during transition

**Action Items:**
- [ ] Write CrossFadeTransition tests
- [ ] Run tests and verify all pass
- [ ] Visual test: See crossfade transition

---

## Step 4.5: SceneManager Transition Integration

### Objective
Integrate transitions into SceneManager so scenes can be switched with transitions.

### Design

**Transition Flow:**
1. User calls `switchScene(path, transition)`
2. SceneManager stores transition and scene handles
3. Old scene is rendered to texture A
4. New scene is loaded and rendered to texture B
5. Transition processes over multiple frames
6. When complete, old scene is unloaded, new scene becomes active

### Detailed Implementation Steps

#### Step 4.5.1: Add Transition State to SceneManager

**File:** `engine/core/scene/SceneManager.hpp` (extend existing from Phase 3)

Add to private members:
```cpp
class SceneManager {
    // ... existing members ...
    
private:
    // ... existing private members ...
    
    // Transition state
    std::unique_ptr<SceneTransition> _activeTransition;
    SceneHandle _transitionFrom;  // Scene transitioning from
    SceneHandle _transitionTo;    // Scene transitioning to
    
    /**
     * Processes the active transition (call from update()).
     */
    void processTransition(float dt);
    
    /**
     * Finalizes the scene switch after transition completes.
     */
    void finalizeSceneSwitch();
    
    // ... rest of private members ...
};
```

**Action Items:**
- [ ] Add transition state members to SceneManager
- [ ] Add forward declaration for SceneTransition
- [ ] Add processTransition() and finalizeSceneSwitch() declarations
- [ ] Verify compilation

#### Step 4.5.2: Implement Transition Processing

**File:** `engine/core/scene/SceneManager.cpp`

1. **Include transition headers:**
   ```cpp
   #include "SceneManager.hpp"
   #include "transitions/SceneTransition.hpp"
   #include "transitions/FadeTransition.hpp"
   #include "transitions/SlideTransition.hpp"
   #include "transitions/CrossFadeTransition.hpp"
   ```

2. **Update switchScene() with transition:**
   ```cpp
   void SceneManager::switchScene(const std::filesystem::path& path, 
                                   std::unique_ptr<SceneTransition> transition) {
       if (!transition) {
           // No transition, use basic switch
           switchScene(path);
           return;
       }
       
       // Get current active scene
       SceneHandle fromHandle = _activeScene;
       
       // Load new scene (but don't activate yet)
       SceneHandle toHandle = loadScene(path);
       Scene* toScene = getScene(toHandle);
       if (!toScene) {
           return;  // Failed to load
       }
       
       // Don't activate new scene yet - transition will handle it
       if (toScene->getState() == SceneState::Unloaded) {
           toScene->onLoad();
       }
       // Keep it paused during transition
       
       // Store transition state
       _activeTransition = std::move(transition);
       _transitionFrom = fromHandle;
       _transitionTo = toHandle;
       
       // Emit transition started event
       std::string fromName = fromHandle.isValid() && getScene(fromHandle) 
           ? getScene(fromHandle)->getName() : "";
       std::string toName = toScene->getName();
       _events.emit(events::SceneTransitionStarted{fromHandle, toHandle, "transition"});
   }
   ```

3. **Implement processTransition():**
   ```cpp
   void SceneManager::processTransition(float dt) {
       if (!_activeTransition) {
           return;  // No active transition
       }
       
       // Update transition
       _activeTransition->update(dt);
       
       // Check if complete
       if (_activeTransition->isComplete()) {
           finalizeSceneSwitch();
       }
   }
   ```

4. **Implement finalizeSceneSwitch():**
   ```cpp
   void SceneManager::finalizeSceneSwitch() {
       if (!_activeTransition) {
           return;
       }
       
       // Unload old scene
       if (_transitionFrom.isValid()) {
           Scene* fromScene = getScene(_transitionFrom);
           if (fromScene) {
               fromScene->onDeactivate();
               // Don't unload immediately - let caller decide
               // Or unload here: unloadScene(_transitionFrom);
           }
       }
       
       // Activate new scene
       Scene* toScene = getScene(_transitionTo);
       if (toScene) {
           toScene->onActivate();
           _activeScene = _transitionTo;
           
           // Update stack
           _sceneStack.clear();
           _sceneStack.push_back(_transitionTo);
       }
       
       // Emit transition completed event
       _events.emit(events::SceneTransitionCompleted{_transitionFrom, _transitionTo});
       
       // Clear transition state
       _activeTransition.reset();
       _transitionFrom = SceneHandle::invalid();
       _transitionTo = SceneHandle::invalid();
   }
   ```

5. **Update update() method:**
   ```cpp
   void SceneManager::update(float dt) {
       // Process active transition
       if (_activeTransition) {
           processTransition(dt);
       }
       
       // Other per-frame updates...
   }
   ```

**Action Items:**
- [ ] Implement transition state management
- [ ] Update switchScene() to accept transition
- [ ] Implement processTransition()
- [ ] Implement finalizeSceneSwitch()
- [ ] Update update() to process transitions
- [ ] Test transition lifecycle

#### Step 4.5.3: Add Convenience Methods

**File:** `engine/core/scene/SceneManager.hpp`

Add convenience methods:
```cpp
class SceneManager {
    // ... existing methods ...
    
    /**
     * Switches to a new scene with a fade transition.
     * @param path - Path to new scene
     * @param duration - Transition duration in seconds
     * @param color - Fade color (default: black)
     */
    void switchSceneFade(const std::filesystem::path& path, 
                         float duration = 0.5f, 
                         glm::vec4 color = {0.0f, 0.0f, 0.0f, 1.0f});
    
    /**
     * Switches to a new scene with a slide transition.
     * @param path - Path to new scene
     * @param duration - Transition duration in seconds
     * @param direction - Slide direction
     */
    void switchSceneSlide(const std::filesystem::path& path,
                          float duration = 0.5f,
                          SlideDirection direction = SlideDirection::Left);
    
    /**
     * Switches to a new scene with a crossfade transition.
     * @param path - Path to new scene
     * @param duration - Transition duration in seconds
     */
    void switchSceneCrossFade(const std::filesystem::path& path,
                              float duration = 0.5f);
    
    // ... rest of class ...
};
```

**File:** `engine/core/scene/SceneManager.cpp`

Implement convenience methods:
```cpp
void SceneManager::switchSceneFade(const std::filesystem::path& path, 
                                    float duration, 
                                    glm::vec4 color) {
    auto transition = std::make_unique<FadeTransition>(duration, color);
    switchScene(path, std::move(transition));
}

void SceneManager::switchSceneSlide(const std::filesystem::path& path,
                                     float duration,
                                     SlideDirection direction) {
    auto transition = std::make_unique<SlideTransition>(duration, direction);
    switchScene(path, std::move(transition));
}

void SceneManager::switchSceneCrossFade(const std::filesystem::path& path,
                                        float duration) {
    auto transition = std::make_unique<CrossFadeTransition>(duration);
    switchScene(path, std::move(transition));
}
```

**Action Items:**
- [ ] Add convenience methods to SceneManager
- [ ] Implement convenience methods
- [ ] Test convenience methods work

---

## Step 4.6: Renderer Integration for Transitions

### Objective
Enable the renderer to render scenes to textures and composite them with transitions.

### Design

**Rendering Flow During Transition:**
1. Render old scene to Surface A (texture)
2. Render new scene to Surface B (texture)
3. Transition renders both textures with effect
4. Present result to screen

**Renderer Changes:**
- Add methods to render scenes to surfaces
- Add methods to render fullscreen textures/quads
- Add transition rendering support

### Detailed Implementation Steps

#### Step 4.6.1: Add Scene Rendering Surfaces to Renderer

**File:** `engine/renderer2d/Renderer2D.hpp`

Add to private members:
```cpp
class Renderer2D {
    // ... existing members ...
    
private:
    // ... existing private members ...
    
    // Transition render targets
    Surface _sceneABuffer;  // Buffer for old scene during transition
    Surface _sceneBBuffer;  // Buffer for new scene during transition
    bool _transitionActive{false};  // True when rendering transition
    
    // ... rest of private members ...
};
```

Add public methods:
```cpp
class Renderer2D {
    // ... existing methods ...
    
    /**
     * Begins rendering a scene (for transition support).
     * Renders to a transition buffer instead of main surface.
     * 
     * @param bufferIndex - 0 for scene A (old), 1 for scene B (new)
     */
    void beginScene(int bufferIndex);
    
    /**
     * Ends rendering a scene.
     */
    void endScene();
    
    /**
     * Renders a transition effect.
     * 
     * @param transition - Transition to render
     * @param screenWidth - Screen width
     * @param screenHeight - Screen height
     */
    void renderTransition(SceneTransition& transition, int screenWidth, int screenHeight);
    
    /**
     * Gets the texture for a scene buffer.
     * 
     * @param bufferIndex - 0 for scene A, 1 for scene B
     * @returns Reference to texture
     */
    const gl::Texture2D& getSceneBufferTexture(int bufferIndex) const;
    
    // ... rest of class ...
};
```

**Action Items:**
- [ ] Add transition buffer surfaces to Renderer2D
- [ ] Add beginScene()/endScene() methods
- [ ] Add renderTransition() method
- [ ] Add getSceneBufferTexture() method
- [ ] Verify compilation

#### Step 4.6.2: Implement Scene Buffer Management

**File:** `engine/renderer2d/Renderer2D.cpp` (or in header if inline)

1. **Update initialize() to create buffers:**
   ```cpp
   void Renderer2D::initialize(int width, int height) {
       resizeTargets(width, height);
       
       // Initialize transition buffers
       _sceneABuffer.initialize(width, height);
       _sceneBBuffer.initialize(width, height);
       
       // ... rest of initialize() ...
   }
   ```

2. **Update resizeTargets() to resize buffers:**
   ```cpp
   void Renderer2D::resizeTargets(int w, int h) {
       // ... existing resize code ...
       
       // Resize transition buffers
       if (_sceneABuffer.width() > 0) {
           _sceneABuffer.resize(w, h);
           _sceneBBuffer.resize(w, h);
       }
   }
   ```

3. **Implement beginScene():**
   ```cpp
   void Renderer2D::beginScene(int bufferIndex) {
       _transitionActive = true;
       
       Surface* target = (bufferIndex == 0) ? &_sceneABuffer : &_sceneBBuffer;
       target->bind();
       glViewport(0, 0, target->width(), target->height());
       target->clear(0.0f, 0.0f, 0.0f, 1.0f, true);
   }
   ```

4. **Implement endScene():**
   ```cpp
   void Renderer2D::endScene() {
       // Flush any pending draws
       for (auto& batch : _batches) {
           batch.second.flush();
       }
       
       // Unbind scene buffer
       glBindFramebuffer(GL_FRAMEBUFFER, 0);
   }
   ```

5. **Implement renderTransition():**
   ```cpp
   void Renderer2D::renderTransition(SceneTransition& transition, 
                                      int screenWidth, 
                                      int screenHeight) {
       // Get textures from buffers
       const gl::Texture2D& oldSceneTex = _sceneABuffer.color();
       const gl::Texture2D& newSceneTex = _sceneBBuffer.color();
       
       // Let transition render itself
       transition.render(*this, oldSceneTex, newSceneTex, screenWidth, screenHeight);
       
       _transitionActive = false;
   }
   ```

6. **Implement getSceneBufferTexture():**
   ```cpp
   const gl::Texture2D& Renderer2D::getSceneBufferTexture(int bufferIndex) const {
       if (bufferIndex == 0) {
           return _sceneABuffer.color();
       } else {
           return _sceneBBuffer.color();
       }
   }
   ```

**Action Items:**
- [ ] Implement scene buffer management
- [ ] Update initialize() and resizeTargets()
- [ ] Implement beginScene()/endScene()
- [ ] Implement renderTransition()
- [ ] Test buffer creation and rendering

#### Step 4.6.3: Update Render Loop for Transitions

**File:** `engine/core/app/Application.cpp` (or wherever rendering happens)

Update render loop to handle transitions:
```cpp
void Application::render() {
    // Check if transition is active
    SceneManager& sceneManager = getSceneManager();
    SceneTransition* activeTransition = sceneManager.getActiveTransition();
    
    if (activeTransition) {
        // Render old scene to buffer A
        _renderer.beginScene(0);
        // Render old scene entities...
        _renderer.endScene();
        
        // Render new scene to buffer B
        _renderer.beginScene(1);
        // Render new scene entities...
        _renderer.endScene();
        
        // Render transition
        int width = /* get screen width */;
        int height = /* get screen height */;
        _renderer.renderTransition(*activeTransition, width, height);
    } else {
        // Normal rendering
        _renderer.beginFrame();
        // Render active scene...
        _renderer.endFrame();
    }
}
```

**Note:** This depends on how your rendering system works. You may need to integrate with existing render systems.

**Action Items:**
- [ ] Understand current rendering loop
- [ ] Add transition rendering support
- [ ] Test transition rendering works
- [ ] Verify both scenes render correctly

#### Step 4.6.4: Implement Fullscreen Rendering Helpers

**File:** `engine/renderer2d/Renderer2D.cpp`

Implement the fullscreen rendering methods needed by transitions:
```cpp
void Renderer2D::drawFullscreenTexture(const gl::Texture2D& texture, float alpha) {
    // Use screen shader to render texture
    // This is a simplified version - actual implementation depends on your shader system
    
    auto screenShader = _shaders.find(_screenShader);
    if (screenShader == _shaders.end()) {
        return;
    }
    
    screenShader->second.bind();
    
    // Set texture
    texture.bindToSlot(0);
    
    // Set alpha uniform (if shader supports it)
    // screenShader->second.setUniform("u_alpha", alpha);
    
    // Render fullscreen quad
    // This requires a fullscreen quad VAO or similar
    // For now, placeholder
    
    screenShader->second.unbind();
}

void Renderer2D::drawFullscreenTexture(const gl::Texture2D& texture, float alpha, const glm::vec2& offset) {
    // Similar to above, but with position offset
    // Requires transform matrix or offset uniform
    // Placeholder for now
}

void Renderer2D::drawFullscreenQuad(const glm::vec4& color) {
    // Render a fullscreen colored quad
    // Can use a simple shader or add to existing screen shader
    // Placeholder for now
}
```

**Note:** These implementations are placeholders. The actual implementation depends on:
- Your shader system (how to set uniforms)
- Your quad rendering system (fullscreen quad VAO)
- Your blending setup

**Action Items:**
- [ ] Implement basic fullscreen rendering
- [ ] Test fullscreen texture rendering
- [ ] Test fullscreen quad rendering
- [ ] Enhance based on actual shader/rendering system

---

## Step 4.7: ShaderTransition Placeholder

### Objective
Create a placeholder interface for future shader-based transitions, allowing custom transition effects via shaders.

### Detailed Implementation Steps

#### Step 4.7.1: Create ShaderTransition Header

**File:** `engine/core/scene/transitions/ShaderTransition.hpp`

```cpp
#pragma once

#include "SceneTransition.hpp"
#include "../../renderer2d/ShaderHandle.hpp"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <variant>

/**
 * Shader-based transition for custom transition effects.
 * 
 * This is a placeholder for future implementation.
 * Allows transitions to be defined via shaders for maximum flexibility.
 * 
 * Planned JSON format:
 * {
 *   "name": "DiamondWipe",
 *   "type": "shader",
 *   "duration": 1.0,
 *   "shader": {
 *     "vertex": "assets/shaders/transitions/fullscreen.vert.glsl",
 *     "fragment": "assets/shaders/transitions/diamond_wipe.frag.glsl"
 *   },
 *   "uniforms": {
 *     "u_diamondSize": 0.1,
 *     "u_edgeSoftness": 0.02
 *   }
 * }
 */
class ShaderTransition : public SceneTransition {
public:
    /**
     * Creates a shader-based transition.
     * 
     * @param duration - Transition duration in seconds
     * @param shader - Shader handle for the transition shader
     */
    ShaderTransition(float duration, ShaderHandle shader);
    
    /**
     * Sets a uniform value for the transition shader.
     * 
     * @param name - Uniform name
     * @param value - Uniform value (float, vec2, or vec4)
     */
    void setUniform(const std::string& name, float value);
    void setUniform(const std::string& name, glm::vec2 value);
    void setUniform(const std::string& name, glm::vec4 value);
    
    /**
     * Renders the shader-based transition.
     */
    void render(
        Renderer2D& renderer,
        const gl::Texture2D& oldSceneTexture,
        const gl::Texture2D& newSceneTexture,
        int screenWidth,
        int screenHeight
    ) override;
    
private:
    ShaderHandle _shader;
    std::unordered_map<std::string, std::variant<float, glm::vec2, glm::vec4>> _uniforms;
};
```

**File:** `engine/core/scene/transitions/ShaderTransition.cpp`

Basic placeholder implementation:
```cpp
#include "ShaderTransition.hpp"
#include "../../renderer2d/Renderer2D.hpp"

ShaderTransition::ShaderTransition(float duration, ShaderHandle shader)
    : SceneTransition(duration), _shader(shader) {
}

void ShaderTransition::setUniform(const std::string& name, float value) {
    _uniforms[name] = value;
}

void ShaderTransition::setUniform(const std::string& name, glm::vec2 value) {
    _uniforms[name] = value;
}

void ShaderTransition::setUniform(const std::string& name, glm::vec4 value) {
    _uniforms[name] = value;
}

void ShaderTransition::render(
    Renderer2D& renderer,
    const gl::Texture2D& oldSceneTexture,
    const gl::Texture2D& newSceneTexture,
    int screenWidth,
    int screenHeight
) {
    // Placeholder implementation
    // TODO: Implement shader-based rendering
    // 1. Bind transition shader
    // 2. Set uniforms (progress, textures, custom uniforms)
    // 3. Render fullscreen quad
    // 4. Unbind shader
}
```

**Action Items:**
- [ ] Create ShaderTransition header and implementation
- [ ] Document planned JSON format
- [ ] Create placeholder implementation
- [ ] Document what needs to be implemented in future

---

## Step 4.8: Integration & Comprehensive Testing

### Objective
Ensure all transition components work together correctly and integrate properly with the rendering system.

### Integration Points

#### Step 4.8.1: Verify Transition Lifecycle

**Test that transitions work end-to-end:**
- [ ] switchScene() with transition starts transition
- [ ] Transition processes over multiple frames
- [ ] Transition completes and scenes switch
- [ ] Events are emitted correctly
- [ ] Old scene is unloaded after transition

#### Step 4.8.2: Verify Rendering Integration

**Test that rendering works with transitions:**
- [ ] Old scene renders to buffer A
- [ ] New scene renders to buffer B
- [ ] Transition renders both buffers correctly
- [ ] No visual artifacts or glitches
- [ ] Performance is acceptable

#### Step 4.8.3: End-to-End Integration Test

**Create comprehensive test:**
```cpp
TEST(SceneManager, EndToEnd_Transition) {
    // Setup
    World world;
    AssetManager assets("test_assets");
    EventBus events;
    SceneManager manager(world, assets, events);
    Renderer2D renderer;
    renderer.initialize(800, 600);
    
    // Load initial scene
    SceneHandle scene1 = manager.loadScene("scene1.json");
    
    // Switch with fade transition
    auto fade = std::make_unique<FadeTransition>(1.0f);
    manager.switchScene("scene2.json", std::move(fade));
    
    // Simulate frames
    for (int i = 0; i < 60; ++i) {  // 1 second at 60 FPS
        float dt = 1.0f / 60.0f;
        manager.update(dt);
        
        // Render transition
        if (manager.getActiveTransition()) {
            // Render scenes to buffers
            renderer.beginScene(0);
            // Render old scene...
            renderer.endScene();
            
            renderer.beginScene(1);
            // Render new scene...
            renderer.endScene();
            
            // Render transition
            renderer.renderTransition(*manager.getActiveTransition(), 800, 600);
        }
    }
    
    // Verify transition completed
    EXPECT_FALSE(manager.getActiveTransition());
    EXPECT_EQ(manager.getActiveSceneHandle(), /* scene2 handle */);
}
```

**Action Items:**
- [ ] Write end-to-end transition test
- [ ] Run test and verify all functionality
- [ ] Fix any integration issues

#### Step 4.8.4: Visual Testing

**Test transitions visually:**
- [ ] Fade transition looks smooth
- [ ] Slide transition works in all directions
- [ ] Crossfade transition looks correct
- [ ] No visual glitches or artifacts
- [ ] Transitions complete at correct time

**Action Items:**
- [ ] Create test scenes for visual testing
- [ ] Test each transition type visually
- [ ] Verify smoothness and correctness
- [ ] Document any issues

---

## Phase 4 Completion Checklist

### Functional Requirements
- [ ] SceneTransition base class works
- [ ] FadeTransition works correctly
- [ ] SlideTransition works in all directions
- [ ] CrossFadeTransition works correctly
- [ ] SceneManager integrates transitions
- [ ] Transitions process over multiple frames
- [ ] Transitions complete and scenes switch
- [ ] Renderer can render scenes to buffers
- [ ] Transition rendering works
- [ ] Events are emitted for transitions

### Code Quality
- [ ] Code compiles without warnings
- [ ] All functions documented
- [ ] Error handling implemented
- [ ] Memory safety verified (no leaks)
- [ ] Code follows project conventions

### Integration
- [ ] Transitions integrate with SceneManager
- [ ] Renderer supports transition rendering
- [ ] Works with Phase 1, 2, 3 components
- [ ] Ready for future enhancements (ShaderTransition)

### Testing
- [ ] Base transition tests pass
- [ ] FadeTransition tests pass
- [ ] SlideTransition tests pass
- [ ] CrossFadeTransition tests pass
- [ ] Integration tests pass
- [ ] Visual tests pass

---

## Estimated Time Breakdown

- **Step 4.1 (Base Class):** 4-5 hours
- **Step 4.2 (FadeTransition):** 6-8 hours
- **Step 4.3 (SlideTransition):** 6-8 hours
- **Step 4.4 (CrossFadeTransition):** 4-5 hours
- **Step 4.5 (SceneManager Integration):** 5-6 hours
- **Step 4.6 (Renderer Integration):** 8-10 hours
- **Step 4.7 (ShaderTransition Placeholder):** 2-3 hours
- **Step 4.8 (Integration & Testing):** 5-7 hours

**Total Phase 4:** 40-52 hours

---

## Common Pitfalls & Solutions

### Pitfall 1: Renderer Not Ready
**Issue:** Renderer doesn't support fullscreen texture rendering
**Solution:** Implement fullscreen rendering helpers first, or use existing blit functionality

### Pitfall 2: Alpha Blending Issues
**Issue:** Transitions don't blend correctly
**Solution:** Ensure proper OpenGL blend state, check alpha values

### Pitfall 3: Transition Never Completes
**Issue:** update() not called or dt is 0
**Solution:** Ensure SceneManager::update() is called every frame

### Pitfall 4: Scenes Not Rendered to Buffers
**Issue:** Old/new scenes not captured before transition
**Solution:** Ensure scenes are rendered to buffers before transition starts

### Pitfall 5: Memory Leaks with Transitions
**Issue:** Transition objects not cleaned up
**Solution:** Use smart pointers, ensure cleanup in finalizeSceneSwitch()

---

## Dependencies on Previous Phases

Phase 4 requires:
- ✅ Phase 1: Transform2D, TransformSystem
- ✅ Phase 2: Scene, SceneManager basic
- ✅ Phase 3: SceneManager with stack, events, update loop
- ✅ Renderer2D with Surface support

If previous phases are not complete, Phase 4 cannot proceed.

---

## Renderer Requirements

Phase 4 requires these renderer capabilities:
- [ ] Render scenes to off-screen surfaces (framebuffers)
- [ ] Render fullscreen textures with alpha
- [ ] Render fullscreen quads with color
- [ ] Support for texture offset/position
- [ ] Proper alpha blending

If renderer doesn't support these, they must be implemented as part of Phase 4.

---

## Next Steps After Phase 4

Once Phase 4 is complete:
1. ✅ Scene transitions are functional
2. ✅ Multiple transition types available
3. ✅ Extensible for custom transitions
4. ✅ Ready for ShaderTransition implementation (future)
5. ✅ Ready for Phase 5 (serialization)

**Phase 4 provides professional scene transitions - ensure they're smooth and visually appealing!**
