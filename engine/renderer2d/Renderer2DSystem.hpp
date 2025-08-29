#pragma once

#include <stdexcept>

#include "../core/app/Application.hpp"
#include "../core/ecs/World.hpp"
#include "../core/ecs/system/System.hpp"
#include "../physics2d/TransformComponent.hpp"
#include "Renderer2D.hpp"
#include "SpriteComponent.hpp"

class Renderer2DSystem : public System {
 public:
  Renderer2DSystem(Renderer2D& renderer) : _renderer(&renderer) {}
  ~Renderer2DSystem() = default;

  void update(World& world, float dt) override {
    if (_renderer == nullptr) {
      JM_LOG_CRITICAL("Renderer2D was not passed to Renderer2DSystem");
      throw std::runtime_error("Renderer2D was not passed to Rendeer2DSystem");
    }

    for (auto [entity, sprite, trans] : world.view<SpriteComponent, TransformComponent>()) {
      _renderer->drawSprite(trans->toMatrix(), sprite->color, sprite->texRect, sprite->layer, sprite->texture);
    }
  }

  const char* name() const { return "Renderer2DSystem"; }

 private:
  Renderer2D* _renderer = nullptr;
};
