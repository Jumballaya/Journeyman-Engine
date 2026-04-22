#pragma once

#include <glm/glm.hpp>

#include "../Renderer2D.hpp"
#include "../shaders.hpp"
#include "PostEffect.hpp"

enum class BuiltinEffectId { Passthrough, Grayscale, Blur, Pixelate, ColorShift };

namespace posteffects::builtins {

inline constexpr const char* grayscale_fragment_shader = R"(
)" JM_GLSL_VERSION R"(

in vec2 v_texCoord;
out vec4 outColor;

uniform sampler2D u_primary;

void main() {
  vec4 c = texture(u_primary, v_texCoord);
  float g = dot(c.rgb, vec3(0.299, 0.587, 0.114));
  outColor = vec4(vec3(g), c.a);
}
)";

inline constexpr const char* blur_fragment_shader = R"(
)" JM_GLSL_VERSION R"(

in vec2 v_texCoord;
out vec4 outColor;

uniform sampler2D u_primary;
uniform vec2 u_resolution;
uniform float u_radius;

const float weights[5] = float[5](0.0625, 0.25, 0.375, 0.25, 0.0625);

void main() {
  vec2 texel = u_radius / u_resolution;
  vec4 sum = vec4(0.0);
  for (int y = -2; y <= 2; ++y) {
    for (int x = -2; x <= 2; ++x) {
      vec2 offset = vec2(float(x), float(y)) * texel;
      float w = weights[x + 2] * weights[y + 2];
      sum += texture(u_primary, v_texCoord + offset) * w;
    }
  }
  outColor = sum;
}
)";

inline constexpr const char* pixelate_fragment_shader = R"(
)" JM_GLSL_VERSION R"(

in vec2 v_texCoord;
out vec4 outColor;

uniform sampler2D u_primary;
uniform vec2 u_resolution;
uniform float u_pixelSize;

void main() {
  vec2 grid = vec2(u_pixelSize) / u_resolution;
  vec2 snapped = grid * floor(v_texCoord / grid);
  outColor = texture(u_primary, snapped);
}
)";

inline constexpr const char* color_shift_fragment_shader = R"(
)" JM_GLSL_VERSION R"(

in vec2 v_texCoord;
out vec4 outColor;

uniform sampler2D u_primary;
uniform vec3 u_hsvDelta;

vec3 rgb2hsv(vec3 c) {
  vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
  vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
  vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
  float d = q.x - min(q.w, q.y);
  float e = 1.0e-10;
  return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c) {
  vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
  vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
  return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
  vec4 c = texture(u_primary, v_texCoord);
  vec3 hsv = rgb2hsv(c.rgb);
  hsv.x = fract(hsv.x + u_hsvDelta.x);
  hsv.y = clamp(hsv.y + u_hsvDelta.y, 0.0, 1.0);
  hsv.z = clamp(hsv.z + u_hsvDelta.z, 0.0, 1.0);
  outColor = vec4(hsv2rgb(hsv), c.a);
}
)";

inline PostEffect passthrough(Renderer2D& r) {
  PostEffect effect;
  effect.shader = r.createShader(screen_vertex_shader, screen_fragment_shader);
  return effect;
}

inline PostEffect grayscale(Renderer2D& r) {
  PostEffect effect;
  effect.shader = r.createShader(screen_vertex_shader, grayscale_fragment_shader);
  return effect;
}

inline PostEffect blur(Renderer2D& r, float radiusPx = 2.0f) {
  PostEffect effect;
  effect.shader = r.createShader(screen_vertex_shader, blur_fragment_shader);
  effect.uniforms["u_radius"] = radiusPx;
  return effect;
}

inline PostEffect pixelate(Renderer2D& r, float pixelSize = 4.0f) {
  PostEffect effect;
  effect.shader = r.createShader(screen_vertex_shader, pixelate_fragment_shader);
  effect.uniforms["u_pixelSize"] = pixelSize;
  return effect;
}

inline PostEffect colorShift(Renderer2D& r, glm::vec3 hsvDelta = glm::vec3(0.0f)) {
  PostEffect effect;
  effect.shader = r.createShader(screen_vertex_shader, color_shift_fragment_shader);
  effect.uniforms["u_hsvDelta"] = hsvDelta;
  return effect;
}

}  // namespace posteffects::builtins
