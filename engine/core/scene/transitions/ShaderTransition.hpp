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
