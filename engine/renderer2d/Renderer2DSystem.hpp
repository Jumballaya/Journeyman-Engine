#pragma once

#include "../core/ecs/World.hpp"
#include "../core/ecs/system/System.hpp"
#include "../physics2d/TransformComponent.hpp"
#include "Renderer2D.hpp"
#include "../physics2d/Terrain.hpp"
#include "Lights.hpp"
#include "Particles.hpp"
#include "SpriteComponent.hpp"

// Submits every sprite, emitter's particles and stroked terrain to the renderer
// (z = transform z), and the lights they're lit by.
class Renderer2DSystem : public System {
 public:
  explicit Renderer2DSystem(Renderer2D& renderer) : _renderer(renderer) {}

  void update(World& world, float) override {
    _renderer.setLighting(gatherLights(world));
    for (auto [entity, sprite, trans] : world.view<SpriteComponent, TransformComponent>()) {
      if (auto shadow = sprite->shadow.instance(*trans, sprite->color.a, sprite->texRect)) {
        _renderer.drawSprite(shadow->transform, shadow->color, shadow->texRect, sprite->texture, shadow->transform[3].z);
      }
      _renderer.drawSprite(trans->toMatrix(), sprite->color, sprite->texRect, sprite->texture, trans->position.z);
    }
    for (auto [entity, emitter, trans] : world.view<ParticleEmitterComponent, TransformComponent>())
      drawParticles(_renderer, *emitter, trans->position.z);
    for (auto [entity, terrain, trans] : world.view<TerrainComponent, TransformComponent>()) {
      if (terrain->strokeColor.a <= 0.0f) continue;
      const glm::vec2 at(trans->position);  // drawn whatever its layers (none: art only)
      for (const TerrainChain& chain : terrain->chains)
        chain.forEachSegment([&](glm::vec2 a, glm::vec2 b) {
          _renderer.drawLine(at + a, at + b, terrain->strokeColor, terrain->strokeWidth, trans->position.z);
        });
    }
  }

  const char* name() const override { return "Renderer2DSystem"; }

  static Lighting gatherLights(World& world) {
    Lighting lighting;
    for (auto [entity, ambient] : world.view<AmbientLightComponent>()) {
      lighting.on = true;
      lighting.ambient = ambient->color * ambient->energy;
    }
    for (auto [entity, light, trans] : world.view<PointLightComponent, TransformComponent>()) {
      lighting.on = true;
      lighting.lights.push_back({glm::vec2(trans->position) + light->offset, light->color * light->energy, light->radius,
                                 light->falloff, light->height});
    }
    return lighting;
  }

 private:
  Renderer2D& _renderer;
};
