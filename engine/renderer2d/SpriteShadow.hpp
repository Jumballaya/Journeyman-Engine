#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include "../physics2d/TransformComponent.hpp"
#include "SpriteInstance.hpp"

// A second draw of the owner's current sprite, with no companion entity.
// Offset is in world space; scale and rotation follow the owner. A missing
// layer follows just behind the owner, or an explicit layer can sit on terrain.
struct SpriteShadow {
  glm::vec2 offset{8.0f, -8.0f};
  float scale = 1.0f;
  float layer = std::numeric_limits<float>::quiet_NaN();
  glm::vec4 color{0.0f};  // opacity zero disables the shadow

  std::optional<SpriteInstance> instance(const TransformComponent& owner, float opacity, const glm::vec4& uv) const {
    const float alpha = std::clamp(color.a, 0.0f, 1.0f) * std::clamp(opacity, 0.0f, 1.0f);
    if (!(alpha > 0.0f) || !(scale > 0.0f)) return std::nullopt;
    TransformComponent transform = owner;
    transform.position.x += offset.x;
    transform.position.y += offset.y;
    transform.position.z = std::isfinite(layer) ? layer : owner.position.z - 0.01f;
    transform.scale *= scale;
    return SpriteInstance{transform.toMatrix(), {color.r, color.g, color.b, alpha}, uv};
  }
};
