#include "CrossFadeTransition.hpp"
#include "../../renderer2d/Renderer2D.hpp"
#include "../../renderer2d/gl/Texture2D.hpp"

CrossFadeTransition::CrossFadeTransition(float duration)
    : SceneTransition(duration) {
    // Crossfade uses the full duration as a single phase
    // We'll handle this in update() override
}

void CrossFadeTransition::update(float dt) {
    if (_complete) {
        return;
    }
    
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

float CrossFadeTransition::getOldSceneAlpha() const {
    // Old scene fades out over entire duration
    // Use total elapsed time, not phase progress
    float totalProgress = _elapsed / _duration;
    if (totalProgress > 1.0f) totalProgress = 1.0f;
    return 1.0f - totalProgress;  // 1.0 → 0.0
}

float CrossFadeTransition::getNewSceneAlpha() const {
    // New scene fades in over entire duration
    float totalProgress = _elapsed / _duration;
    if (totalProgress > 1.0f) totalProgress = 1.0f;
    return totalProgress;  // 0.0 → 1.0
}

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
        // TODO: Implement renderer.drawFullscreenTexture(oldSceneTexture, oldAlpha);
    }
    
    // Render new scene (if visible) - blends with old scene
    if (newAlpha > 0.001f) {
        // TODO: Implement renderer.drawFullscreenTexture(newSceneTexture, newAlpha);
    }
}
