#pragma once

#include <memory>

// Forward declarations
class Renderer2D;
namespace gl {
    class Texture2D;
}

/**
 * Represents the current phase of a scene transition.
 */
enum class TransitionPhase {
    Out,    // Fading out old scene
    In      // Fading in new scene
};

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
    
    // Movable
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
    float _progress{0.0f};        // Progress in current phase [0.0, 1.0]
    TransitionPhase _phase{TransitionPhase::Out};  // Current phase
    bool _complete{false};        // True when transition is complete
};
