#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>

#include <glm/glm.hpp>

#include "../ShaderHandle.hpp"
#include "../TextureHandle.hpp"

using UniformValue = std::variant<float, int, glm::vec2, glm::vec3, glm::vec4, glm::mat4>;

// Names an effect in a PostEffectChain; 0 = none.
struct PostEffectHandle {
  uint32_t id = 0;

  bool isValid() const { return id != 0; }
  bool operator==(const PostEffectHandle&) const = default;
};

struct PostEffect {
  PostEffectHandle handle;
  ShaderHandle shader;
  std::unordered_map<std::string, UniformValue> uniforms;
  TextureHandle auxTexture;  // bound as u_aux when valid
  bool enabled = true;
};
