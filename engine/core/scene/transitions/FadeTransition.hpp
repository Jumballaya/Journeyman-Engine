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
