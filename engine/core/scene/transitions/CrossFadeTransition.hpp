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
     * Override update() to use single continuous phase instead of Out/In.
     */
    void update(float dt) override;
    
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
