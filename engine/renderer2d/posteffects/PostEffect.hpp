#pragma once

#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

#include "../ShaderHandle.hpp"
#include "../TextureHandle.hpp"
#include "PostEffectHandle.hpp"

using UniformValue = std::variant<float, int, glm::vec2, glm::vec3, glm::vec4, glm::mat4>;

struct PostEffect {
  PostEffectHandle handle;
  ShaderHandle shader;
  std::unordered_map<std::string, UniformValue> uniforms;
  std::optional<TextureHandle> auxTexture;
  bool enabled = true;
};
