#pragma once

#include "../core/ecs/World.hpp"
#include "../core/ecs/system/System.hpp"
#include "../physics2d/TransformComponent.hpp"
#include "Renderer2D.hpp"
#include "SpriteComponent.hpp"

// Submits every sprite to the renderer (z = transform z).
class Renderer2DSystem : public System {
 public:
  explicit Renderer2DSystem(Renderer2D& renderer) : _renderer(renderer) {}

  void update(World& world, float) override {
    for (auto [entity, sprite, trans] : world.view<SpriteComponent, TransformComponent>()) {
      if (auto shadow = sprite->shadow.instance(*trans, sprite->color.a, sprite->texRect)) {
        _renderer.drawSprite(shadow->transform, shadow->color, shadow->texRect, sprite->texture, shadow->transform[3].z);
      }
      _renderer.drawSprite(trans->toMatrix(), sprite->color, sprite->texRect, sprite->texture, trans->position.z);
    }
  }

  const char* name() const override { return "Renderer2DSystem"; }

 private:
  Renderer2D& _renderer;
};
