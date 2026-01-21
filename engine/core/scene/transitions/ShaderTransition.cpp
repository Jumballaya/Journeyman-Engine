#include "ShaderTransition.hpp"
#include "../../renderer2d/Renderer2D.hpp"
#include "../../renderer2d/gl/Texture2D.hpp"

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
    
    (void)renderer;
    (void)oldSceneTexture;
    (void)newSceneTexture;
    (void)screenWidth;
    (void)screenHeight;
}
