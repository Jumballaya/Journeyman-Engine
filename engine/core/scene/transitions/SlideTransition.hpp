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
