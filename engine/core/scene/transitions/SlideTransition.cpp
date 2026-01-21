#include "SlideTransition.hpp"
#include "../../renderer2d/Renderer2D.hpp"
#include "../../renderer2d/gl/Texture2D.hpp"
#include <glm/glm.hpp>

SlideTransition::SlideTransition(float duration, SlideDirection direction)
    : SceneTransition(duration), _direction(direction) {
}

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
            offset.y = currentOffset;   // Slide up (positive Y in screen coords)
            break;
        case SlideDirection::Down:
            offset.y = -currentOffset;  // Slide down (negative Y)
            break;
    }
    
    return offset;
}

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
                offset.x = -distance;  // Start from left
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
            offset.y = -currentOffset;   // Slide in from bottom
            break;
        case SlideDirection::Down:
            offset.y = currentOffset;   // Slide in from top
            break;
    }
    
    return offset;
}

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
        // TODO: Implement renderer.drawFullscreenTexture(oldSceneTexture, 1.0f, oldOffset);
        // Requires renderer support for rendering textures with position offset
    }
    
    // Render new scene with offset (if visible)
    if (_phase == TransitionPhase::In || _progress > 0.0f) {
        // TODO: Implement renderer.drawFullscreenTexture(newSceneTexture, 1.0f, newOffset);
    }
}
