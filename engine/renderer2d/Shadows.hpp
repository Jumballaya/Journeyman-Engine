#pragma once

#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

#include "Lights.hpp"
#include "gl/Shader.hpp"
#include "shaders.hpp"

// Light shadows as a 1D shadow map, the usual 2D way: a row per light slot,
// each texel the distance to the nearest occluder in its direction.
inline constexpr int kShadowAngles = JM_SHADOW_ANGLES;  // a power of 2: the sprite shader wraps columns with &
inline constexpr int kShadowRows = JM_MAX_LIGHTS;      // one per light the sprite shader takes

// One occluder segment drawn into a light's row over the angles [from, to]
// (radians, -π at column 0); a span past ±π is drawn again a turn over.
struct ShadowCaster {
  glm::vec4 segment;  // a.xy, b.xy
  glm::vec2 light;
  glm::vec2 span;  // from, to
  float row;
};

// What lights[row] casts, for those with shadows: the occluder segments near
// it, without closed shapes' sides facing it. Empty: no shadow pass.
std::vector<ShadowCaster> shadowCasters(std::span<const Lighting::Light* const> lights,
                                        std::span<const Lighting::Occluder> occluders);

// Distance from `light` along `angle` to segment a-b (the shadow pass's
// per-texel math); a ray missing it gets its nearer end; nullopt: behind.
std::optional<float> shadowDistance(glm::vec2 light, float angle, glm::vec2 a, glm::vec2 b);
// The angle a column's texel stands for, at its center.
float shadowColumnAngle(int column);

// The GPU shadow map: R32F, kShadowAngles x kShadowRows.
class ShadowMap {
 public:
  void initialize();
  void destroy();
  // Redraws it from `casters`: each texel the nearest one's distance, else far.
  // Leaves its framebuffer bound and blending off.
  void build(std::span<const ShadowCaster> casters);
  void bindToSlot(int slot) const;

 private:
  unsigned _fbo = 0, _texture = 0, _vao = 0, _instances = 0;
  std::optional<gl::Shader> _shader;
};
