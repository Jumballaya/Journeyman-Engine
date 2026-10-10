#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

// Fitting the game's fixed logical screen into a framebuffer of any size: the
// largest whole-pixel rect with the logical aspect, centered, with bars on the
// sides or above and below.
namespace letterbox {

// The game's rect in the framebuffer: x, y from the bottom-left, width, height (px).
inline glm::vec4 fit(int width, int height, int logicalW, int logicalH) {
  const float scale = std::min(static_cast<float>(width) / logicalW, static_cast<float>(height) / logicalH);
  const float w = std::floor(logicalW * scale), h = std::floor(logicalH * scale);
  return {std::floor((width - w) * 0.5f), std::floor((height - h) * 0.5f), w, h};
}

// A framebuffer point (px from the top-left, as windows report the mouse) in
// the game's logical px, from its top-left; outside [0, logical size) when it's
// over a bar.
inline glm::vec2 toLogical(glm::vec2 point, glm::vec4 viewport, int frameHeight, int logicalW) {
  const float scale = viewport.z / static_cast<float>(logicalW);
  if (!(scale > 0.0f)) return glm::vec2(0.0f);
  const float top = static_cast<float>(frameHeight) - (viewport.y + viewport.w);
  return {(point.x - viewport.x) / scale, (point.y - top) / scale};
}

// The inverse: a logical point (px from the game's top-left) in framebuffer px.
inline glm::vec2 toFramebuffer(glm::vec2 logical, glm::vec4 viewport, int frameHeight, int logicalW) {
  const float scale = viewport.z / static_cast<float>(logicalW);
  const float top = static_cast<float>(frameHeight) - (viewport.y + viewport.w);
  return {logical.x * scale + viewport.x, logical.y * scale + top};
}

}  // namespace letterbox
