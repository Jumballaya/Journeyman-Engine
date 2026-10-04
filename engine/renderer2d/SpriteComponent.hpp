#pragma once

#include <glm/glm.hpp>

#include "../core/ecs/component/Component.hpp"
#include "TextureHandle.hpp"

// A textured quad drawn at the entity's transform (scale = half size).
struct SpriteComponent : public Component<SpriteComponent> {
  COMPONENT_NAME("SpriteComponent");
  TextureHandle texture{};
  glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
  glm::vec4 texRect{0.0f, 0.0f, 1.0f, 1.0f};  // u, v, w, h within the texture
};
