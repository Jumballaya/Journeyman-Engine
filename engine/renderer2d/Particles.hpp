#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "../core/ecs/World.hpp"
#include "../core/ecs/component/Component.hpp"
#include "../core/ecs/system/System.hpp"
#include "Renderer2D.hpp"
#include "TextureHandle.hpp"

// Sparks, dust, smoke: small sprites as data, not entities; each flies, falls and
// fades from start to end color and size. Once out, they don't follow the emitter.
struct ParticleEmitterComponent : public Component<ParticleEmitterComponent> {
  COMPONENT_NAME("ParticleEmitterComponent");
  float rate = 0.0f;           // per second while emitting
  uint32_t emitting = 1;       // a script field: 0 stops the stream (what's out lives on)
  uint32_t burst = 0;          // sent out at the next step, then 0 (scripts add to it)
  glm::vec2 offset{0.0f};      // where they come from, from the transform
  glm::vec2 lifetime{0.5f, 1.0f};  // seconds, picked between
  glm::vec2 speed{40.0f, 80.0f};   // world units per second, picked between
  float angle = 90.0f;         // degrees: the way they go (90: up)
  float spread = 360.0f;       // degrees around it
  glm::vec2 gravity{0.0f};
  glm::vec4 startColor{1.0f}, endColor{1.0f, 1.0f, 1.0f, 0.0f};
  float startSize = 2.0f, endSize = 0.0f;  // half sizes, as a sprite's scale
  TextureHandle texture;       // none: solid quads
  glm::vec4 texRect{0.0f, 0.0f, 1.0f, 1.0f};
  uint32_t maxParticles = 256;

  struct Particle {
    glm::vec2 position, velocity;
    float age, life;
  };
  std::vector<Particle> particles;
  float owed = 0.0f;           // part of a particle the stream owes
  uint32_t random = 1;         // xorshift state, from the run's seed
};

// Ages and moves `e`'s particles by dt, sending new ones out from `at`.
void stepParticles(ParticleEmitterComponent& e, glm::vec2 at, float dt);
void drawParticles(Renderer2D& renderer, const ParticleEmitterComponent& e, float z);

// Steps and draws every emitter (at its transform's z).
class ParticleSystem : public System {
 public:
  explicit ParticleSystem(Renderer2D& renderer) : _renderer(renderer) {}
  void update(World& world, float dt) override;
  const char* name() const override { return "ParticleSystem"; }

 private:
  Renderer2D& _renderer;
};
