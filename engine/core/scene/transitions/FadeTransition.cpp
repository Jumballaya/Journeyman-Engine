#include "FadeTransition.hpp"
#include "../../renderer2d/Renderer2D.hpp"
#include "../../renderer2d/gl/Texture2D.hpp"

FadeTransition::FadeTransition(float duration, glm::vec4 color)
    : SceneTransition(duration), _color(color) {
}

void FadeTransition::render(
    Renderer2D& renderer,
    const gl::Texture2D& oldSceneTexture,
    const gl::Texture2D& newSceneTexture,
    int screenWidth,
    int screenHeight
) {
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
        overlayAlpha = 1.0f - _progress;   // 1.0 → 0.0
    }
    
    // Render old scene (if visible)
    if (oldSceneAlpha > 0.001f) {
        // TODO: Implement renderer.drawFullscreenTexture(oldSceneTexture, oldSceneAlpha);
        // For now, placeholder - will need renderer support
    }
    
    // Render new scene (if visible)
    if (newSceneAlpha > 0.001f) {
        // TODO: Implement renderer.drawFullscreenTexture(newSceneTexture, newSceneAlpha);
    }
    
    // Render overlay color
    if (overlayAlpha > 0.001f) {
        glm::vec4 overlayColor = _color;
        overlayColor.a *= overlayAlpha;  // Apply alpha to color
        // TODO: Implement renderer.drawFullscreenQuad(overlayColor);
    }
}
