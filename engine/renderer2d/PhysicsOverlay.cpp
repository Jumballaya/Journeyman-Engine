#include "PhysicsOverlay.hpp"

#include <cmath>
#include <numbers>

#include <glm/gtc/matrix_transform.hpp>

#include "../physics2d/Colliders.hpp"
#include "../physics2d/Terrain.hpp"

namespace {

constexpr float kOnTop = 1.0e6f;  // z: over everything in the world
constexpr glm::vec4 kSolid{1.0f, 0.3f, 0.9f, 0.9f}, kSensor{0.3f, 1.0f, 0.4f, 0.8f}, kCircle{0.3f, 0.9f, 1.0f, 0.8f};
constexpr glm::vec4 kGround{1.0f, 1.0f, 1.0f, 0.9f}, kOneWay{1.0f, 0.9f, 0.2f, 0.9f};

void line(Renderer2D& renderer, glm::vec2 a, glm::vec2 b, glm::vec4 color, float width) {
  const glm::vec2 d = b - a;
  const float length = glm::length(d);
  if (length == 0.0f) return;
  glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3((a + b) * 0.5f, 0.0f));
  m = glm::rotate(m, std::atan2(d.y, d.x), glm::vec3(0.0f, 0.0f, 1.0f));
  m = glm::scale(m, glm::vec3(length * 0.5f, width * 0.5f, 1.0f));  // a quad spans ±scale
  renderer.drawSprite(m, color, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f), TextureHandle{}, kOnTop);
}

void box(Renderer2D& renderer, glm::vec2 center, glm::vec2 half, glm::vec4 color, float width) {
  const glm::vec2 lo = center - half, hi = center + half;
  line(renderer, lo, {hi.x, lo.y}, color, width);
  line(renderer, {hi.x, lo.y}, hi, color, width);
  line(renderer, hi, {lo.x, hi.y}, color, width);
  line(renderer, {lo.x, hi.y}, lo, color, width);
}

}  // namespace

void drawPhysicsOverlay(Renderer2D& renderer, World& world) {
  const float width = 1.5f / renderer.camera().zoom();  // about a pixel and a half, at any zoom
  forEachCollider(world, [&](const Collider& c) {  // the shapes physics uses, not the raw fields
    const Shape& s = c.shape;
    if (s.kind == Shape::Kind::Box) {
      const bool solid = world.getComponent<BoxColliderComponent>(c.entity)->blocksMask != 0;
      box(renderer, s.center, s.half, solid ? kSolid : kSensor, width);
      return;
    }
    constexpr int kSides = 24;
    for (int i = 0; i < kSides; ++i) {
      const float a0 = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / kSides;
      const float a1 = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i + 1) / kSides;
      line(renderer, s.center + s.radius * glm::vec2(std::cos(a0), std::sin(a0)),
           s.center + s.radius * glm::vec2(std::cos(a1), std::sin(a1)), kCircle, width);
    }
  });
  forEachTerrainSegment(world, glm::vec2(-INFINITY), glm::vec2(INFINITY), 0xFFFFFFFFu,
                        [&](const TerrainSegment& s) { line(renderer, s.a, s.b, s.oneWay ? kOneWay : kGround, width * 2.0f); });
}
