#pragma once

#include <array>
#include <string_view>
#include <utility>
#include <vector>

#include "PostEffect.hpp"

// Post-effects that ship with the engine, addressed by name from scripts
// (PostEffect.builtin("vignette")). Bodies use the standard prelude.
struct BuiltinEffect {
  std::string_view name;
  const char* body;
  std::vector<std::pair<const char*, UniformValue>> defaults;
};

inline const std::vector<BuiltinEffect>& builtinEffects() {
  static const std::vector<BuiltinEffect> effects = {
      {"passthrough", R"(
void main() { outColor = texture(u_primary, v_texCoord); }
)", {}},
      {"grayscale", R"(
void main() {
  vec4 c = texture(u_primary, v_texCoord);
  outColor = vec4(vec3(dot(c.rgb, vec3(0.299, 0.587, 0.114))), c.a);
}
)", {}},
      {"blur", R"(
uniform float u_radius;  // pixels
const float w[5] = float[5](0.0625, 0.25, 0.375, 0.25, 0.0625);
void main() {
  vec2 texel = u_radius / u_resolution;
  vec4 sum = vec4(0.0);
  for (int y = -2; y <= 2; ++y)
    for (int x = -2; x <= 2; ++x)
      sum += texture(u_primary, v_texCoord + vec2(x, y) * texel) * w[x + 2] * w[y + 2];
  outColor = sum;
}
)", {{"u_radius", 2.0f}}},
      {"pixelate", R"(
uniform float u_pixelSize;  // pixels
void main() {
  vec2 grid = vec2(u_pixelSize) / u_resolution;
  outColor = texture(u_primary, grid * floor(v_texCoord / grid));
}
)", {{"u_pixelSize", 4.0f}}},
      {"colorshift", R"(
uniform vec3 u_hsvDelta;  // hue, saturation, value offsets
vec3 rgb2hsv(vec3 c) {
  vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
  vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
  vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
  float d = q.x - min(q.w, q.y);
  return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + 1e-10)), d / (q.x + 1e-10), q.x);
}
vec3 hsv2rgb(vec3 c) {
  vec3 p = abs(fract(c.xxx + vec3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
  return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}
void main() {
  vec4 c = texture(u_primary, v_texCoord);
  vec3 hsv = rgb2hsv(c.rgb) + u_hsvDelta;
  outColor = vec4(hsv2rgb(vec3(fract(hsv.x), clamp(hsv.yz, 0.0, 1.0))), c.a);
}
)", {{"u_hsvDelta", glm::vec3(0.0f)}}},
      {"vignette", R"(
uniform float u_strength;  // 0..1
void main() {
  vec4 c = texture(u_primary, v_texCoord);
  vec2 p = (gl_FragCoord.xy - u_viewport.xy) / u_viewport.zw - 0.5;
  float v = smoothstep(0.75, 0.25, length(p * vec2(1.0, 0.9)));
  outColor = vec4(c.rgb * mix(1.0, v, u_strength), c.a);
}
)", {{"u_strength", 0.6f}}},
      {"flash", R"(
uniform vec3 u_color;
uniform float u_amount;  // 0..1
void main() {
  vec4 c = texture(u_primary, v_texCoord);
  outColor = vec4(mix(c.rgb, u_color, clamp(u_amount, 0.0, 1.0)), c.a);
}
)", {{"u_color", glm::vec3(1.0f)}, {"u_amount", 0.0f}}},
  };
  return effects;
}

// The default scene transition: old frame (u_aux) → new frame (u_primary).
inline constexpr const char* kCrossfadeTransition = R"(
void main() {
  outColor = mix(texture(u_aux, v_texCoord), texture(u_primary, v_texCoord), clamp(u_progress, 0.0, 1.0));
}
)";
