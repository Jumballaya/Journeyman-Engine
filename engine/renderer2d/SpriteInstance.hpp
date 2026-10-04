#pragma once

#include <glm/glm.hpp>

struct alignas(16) SpriteInstance {
  glm::mat4 transform;
  alignas(16) glm::vec4 color;
  alignas(16) glm::vec4 texRect;  // [u, v, w, h] in texture UV space
};
