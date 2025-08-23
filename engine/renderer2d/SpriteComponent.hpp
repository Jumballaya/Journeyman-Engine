#pragma once

#include <glm/glm.hpp>

#include "../core/ecs/component/Component.hpp"
#include "TextureHandle.hpp"

struct SpriteComponent : public Component<SpriteComponent> {
  TextureHandle texture{};
  glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
  glm::vec4 texRect{0.0f, 0.0f, 1.0f, 1.0f};
  float layer{0.0f};
};