#include "Shadows.hpp"

#include <cmath>
#include <cstddef>
#include <numbers>

#include "common.hpp"
#include "shaders.hpp"

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

float cross(glm::vec2 a, glm::vec2 b) { return a.x * b.y - a.y * b.x; }

float distanceToSegment(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
  const glm::vec2 e = b - a;
  const float len2 = glm::dot(e, e);
  const float t = len2 > 0.0f ? glm::clamp(glm::dot(p - a, e) / len2, 0.0f, 1.0f) : 0.0f;
  return glm::distance(p, a + t * e);
}

void addCaster(std::vector<ShadowCaster>& out, const Lighting::Light& light, float row, glm::vec2 a, glm::vec2 b) {
  const float from = std::atan2(a.y - light.position.y, a.x - light.position.x);
  float turn = std::atan2(b.y - light.position.y, b.x - light.position.x) - from;  // the way it turns, |turn| ≤ π
  if (turn > kPi) turn -= 2.0f * kPi;
  if (turn < -kPi) turn += 2.0f * kPi;
  const float pad = 2.0f * kPi / kShadowAngles;  // a texel each side: a sliver of a segment still covers one
  const float lo = std::min(from, from + turn) - pad, hi = std::max(from, from + turn) + pad;
  for (const float shift : {0.0f, 2.0f * kPi, -2.0f * kPi}) {
    if (lo + shift < kPi && hi + shift > -kPi) out.push_back({glm::vec4(a, b), light.position, {lo + shift, hi + shift}, row});
  }
}

}  // namespace

std::vector<ShadowCaster> shadowCasters(std::span<const Lighting::Light* const> lights,
                                        std::span<const Lighting::Occluder> occluders) {
  std::vector<ShadowCaster> casters;
  for (size_t row = 0; row < lights.size() && row < kShadowRows; ++row) {
    const Lighting::Light& light = *lights[row];
    if (!light.shadowSoftness) continue;
    for (const Lighting::Occluder& o : occluders) {
      const size_t n = o.points.size(), count = o.closed && n > 2 ? n : (n > 0 ? n - 1 : 0);
      for (size_t i = 0; i < count; ++i) {
        const glm::vec2 a = o.points[i], b = o.points[(i + 1) % n];
        if (a == b) continue;
        // Counterclockwise: outside is to the right. A side facing the light only hides the shape's own inside.
        if (o.closed && cross(b - a, light.position - a) < 0.0f) continue;
        if (distanceToSegment(light.position, a, b) >= light.radius) continue;
        addCaster(casters, light, static_cast<float>(row), a, b);
      }
    }
  }
  return casters;
}

std::optional<float> shadowDistance(glm::vec2 light, float angle, glm::vec2 a, glm::vec2 b) {
  const glm::vec2 dir(std::cos(angle), std::sin(angle)), toA = a - light, e = b - a;
  const float denom = cross(dir, e);
  const float u = std::abs(denom) < 1e-6f ? -1.0f : cross(toA, dir) / denom;  // where along a-b the ray crosses
  // Missing it (a padded texel, or along its line): the nearer end, never nearer than the segment.
  if (u < 0.0f || u > 1.0f) return std::min(glm::length(toA), glm::length(toA + e));
  const float t = cross(toA, e) / denom;
  return t < 0.0f ? std::nullopt : std::optional(t);
}

float shadowColumnAngle(int column) { return (static_cast<float>(column) + 0.5f) / kShadowAngles * 2.0f * kPi - kPi; }

void ShadowMap::initialize() {
  _shader.emplace().load(shadow_vertex_shader, shadow_fragment_shader);
  glGenTextures(1, &_texture);
  glBindTexture(GL_TEXTURE_2D, _texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, kShadowAngles, kShadowRows, 0, GL_RED, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glGenFramebuffers(1, &_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _texture, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  glGenVertexArrays(1, &_vao);
  glBindVertexArray(_vao);
  glGenBuffers(1, &_instances);
  glBindBuffer(GL_ARRAY_BUFFER, _instances);
  const std::pair<int, size_t> attributes[] = {{4, offsetof(ShadowCaster, segment)},
                                               {2, offsetof(ShadowCaster, light)},
                                               {2, offsetof(ShadowCaster, span)},
                                               {1, offsetof(ShadowCaster, row)}};
  for (GLuint i = 0; i < 4; ++i) {
    glEnableVertexAttribArray(i);
    glVertexAttribPointer(i, attributes[i].first, GL_FLOAT, GL_FALSE, sizeof(ShadowCaster),
                          reinterpret_cast<void*>(attributes[i].second));
    glVertexAttribDivisor(i, 1);
  }
  glBindVertexArray(0);
}

void ShadowMap::destroy() {
  if (_fbo) glDeleteFramebuffers(1, &_fbo);
  if (_texture) glDeleteTextures(1, &_texture);
  if (_vao) glDeleteVertexArrays(1, &_vao);
  if (_instances) glDeleteBuffers(1, &_instances);
  _fbo = _texture = _vao = _instances = 0;
  _shader.reset();  // while the context lives
}

void ShadowMap::build(std::span<const ShadowCaster> casters) {
  glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
  glViewport(0, 0, kShadowAngles, kShadowRows);
  glClearColor(1e30f, 0.0f, 0.0f, 0.0f);  // far: nothing in the way
  glClear(GL_COLOR_BUFFER_BIT);
  glBindBuffer(GL_ARRAY_BUFFER, _instances);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(casters.size_bytes()), casters.data(), GL_STREAM_DRAW);
  glEnable(GL_BLEND);
  glBlendEquation(GL_MIN);  // the nearest occluder wins
  _shader->bind();
  glBindVertexArray(_vao);
  glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(casters.size()));
  glBindVertexArray(0);
  _shader->unbind();
  glBlendEquation(GL_FUNC_ADD);
  glDisable(GL_BLEND);
}

void ShadowMap::bindToSlot(int slot) const {
  glActiveTexture(GL_TEXTURE0 + slot);
  glBindTexture(GL_TEXTURE_2D, _texture);
}
