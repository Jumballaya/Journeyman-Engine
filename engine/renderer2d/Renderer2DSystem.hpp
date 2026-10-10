#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

#include "../core/ecs/World.hpp"
#include "../core/ecs/system/System.hpp"
#include "../physics2d/TransformComponent.hpp"
#include "Renderer2D.hpp"
#include "../physics2d/Colliders.hpp"
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
    for (auto [entity, terrain, trans] : world.view<GroundComponent, TransformComponent>()) {
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
                                 light->falloff, light->height,
                                 light->shadows ? std::optional(light->shadowSoftness) : std::nullopt});
    }
    if (std::ranges::any_of(lighting.lights, [](const auto& l) { return l.shadowSoftness.has_value(); }))
      lighting.occluders = gatherOccluders(world);
    return lighting;
  }

  // LightOccluderComponent entities' colliders (a circle as a polygon) and occluding terrain chains.
  static std::vector<Lighting::Occluder> gatherOccluders(World& world) {
    std::vector<Lighting::Occluder> occluders;
    forEachCollider(world, [&](const Collider& c) {
      if (!world.hasComponent<LightOccluderComponent>(c.entity)) return;
      const Shape& s = c.shape;
      std::vector<glm::vec2> outline;
      if (s.kind == Shape::Kind::Box) {
        outline = {s.center - s.half, s.center + glm::vec2(s.half.x, -s.half.y), s.center + s.half,
                   s.center + glm::vec2(-s.half.x, s.half.y)};
      } else {
        constexpr int kSides = 16;
        for (int i = 0; i < kSides; ++i) {
          const float a = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / kSides;
          outline.push_back(s.center + s.radius * glm::vec2(std::cos(a), std::sin(a)));
        }
      }
      occluders.push_back({std::move(outline), true});
    });
    for (auto [entity, terrain, trans] : world.view<GroundComponent, TransformComponent>()) {
      for (const TerrainChain& chain : terrain->chains) {
        if (!chain.occludes()) continue;
        const std::vector<glm::vec2>& local = chain.points();
        Lighting::Occluder o{{}, chain.closed() && local.size() > 2};
        for (const glm::vec2 p : local) o.points.push_back(glm::vec2(trans->position) + p);
        double area = 0.0;  // twice the signed area, of the local points (precise): below 0, clockwise
        for (size_t i = 0; o.closed && i < local.size(); ++i) {
          const glm::dvec2 a = local[i], b = local[(i + 1) % local.size()];
          area += a.x * b.y - b.x * a.y;
        }
        if (area < 0.0f) std::ranges::reverse(o.points);
        occluders.push_back(std::move(o));
      }
    }
    return occluders;
  }

 private:
  Renderer2D& _renderer;
};
