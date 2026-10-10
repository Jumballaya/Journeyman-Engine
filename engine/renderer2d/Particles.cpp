#include "Particles.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <glm/gtc/matrix_transform.hpp>

#include "../physics2d/TransformComponent.hpp"

namespace {

// A number in [lo, hi] from the emitter's own random stream.
float pick(ParticleEmitterComponent& e, float lo, float hi) {
  uint32_t& x = e.random;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return lo + (hi - lo) * static_cast<float>(x >> 8) / static_cast<float>(1u << 24);
}

void emit(ParticleEmitterComponent& e, glm::vec2 at, uint32_t count) {
  constexpr float kRadians = std::numbers::pi_v<float> / 180.0f;
  for (uint32_t i = 0; i < count && e.particles.size() < e.maxParticles; ++i) {
    const float heading = (e.angle + pick(e, -0.5f, 0.5f) * e.spread) * kRadians;
    const float speed = pick(e, e.speed.x, e.speed.y);
    e.particles.push_back({at, speed * glm::vec2(std::cos(heading), std::sin(heading)), 0.0f,
                           std::max(pick(e, e.lifetime.x, e.lifetime.y), 1e-3f)});
  }
}

}  // namespace

void stepParticles(ParticleEmitterComponent& e, glm::vec2 at, float dt) {
  if (!(dt > 0.0f)) return;  // paused (or nonsense): everything holds still
  for (auto& p : e.particles) {
    p.age += dt;
    p.velocity += e.gravity * dt;
    p.position += p.velocity * dt;
  }
  std::erase_if(e.particles, [](const auto& p) { return p.age >= p.life; });
  emit(e, at, std::exchange(e.burst, 0u));
  if (!e.emitting || !(e.rate > 0.0f) || !std::isfinite(e.rate)) {
    e.owed = 0.0f;
    return;
  }
  const double owed = static_cast<double>(e.owed) + static_cast<double>(e.rate) * dt;
  const double whole = std::floor(owed);
  e.owed = static_cast<float>(owed - whole);
  emit(e, at, static_cast<uint32_t>(std::min(whole, static_cast<double>(e.maxParticles))));  // more than fit: no use
}

void drawParticles(Renderer2D& renderer, const ParticleEmitterComponent& e, float z) {
  for (const auto& p : e.particles) {
    const float t = p.age / p.life;
    const float size = e.startSize + (e.endSize - e.startSize) * t;
    if (size <= 0.0f) continue;
    const glm::mat4 m = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(p.position, z)), glm::vec3(size, size, 1.0f));
    renderer.drawSprite(m, e.startColor + (e.endColor - e.startColor) * t, e.texRect, e.texture, z);
  }
}

void ParticleSystem::update(World& world, float dt) {
  for (auto [entity, emitter, trans] : world.view<ParticleEmitterComponent, TransformComponent>())
    stepParticles(*emitter, glm::vec2(trans->position) + emitter->offset, dt);
}
