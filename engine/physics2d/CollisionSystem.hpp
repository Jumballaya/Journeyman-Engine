#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

#include "../core/ecs/World.hpp"
#include "../core/ecs/entity/EntityId.hpp"
#include "../core/ecs/system/System.hpp"
#include "../core/events/EventBus.hpp"
#include "../core/scripting/ScriptComponent.hpp"
#include "BoxColliderComponent.hpp"
#include "Physics2DEvents.hpp"
#include "TransformComponent.hpp"
#include "VelocityComponent.hpp"

class CollisionSystem : public System {
 public:
  CollisionSystem(EventBus& bus, ScriptManager& script) : _eventBus(bus), _scriptManager(script) {
    _proxies.reserve(128);
  };

  void update(World& world, float dt) override {
    if (!std::isfinite(dt) || dt < 0.0f) dt = 0.0f;
    constexpr float kMaxDt = 1.0f / 20.0f;  // 50 ms
    if (dt > kMaxDt) dt = kMaxDt;

    _proxies.clear();

    for (auto [entity, trans, collider] : world.view<TransformComponent, BoxColliderComponent>()) {
      const glm::vec2 center = glm::vec2{trans->position.x, trans->position.y} + collider->offset;
      const glm::vec2 half = collider->halfExtents;
      Proxy p;
      p.e = entity;
      p.center = center;
      p.min = center - half;
      p.max = center + half;
      p.layerMask = collider->layerMask;
      p.collidesWithMask = collider->collidesWithMask;
      if (auto v = world.getComponent<VelocityComponent>(entity)) {
        p.hasVel = true;
        p.vel = glm::vec2{v->velocity.x, v->velocity.y};
      } else {
        p.hasVel = false;
        p.vel = glm::vec2{0.0f, 0.0f};
      }
      _proxies.emplace_back(p);
    }

    const size_t n = _proxies.size();
    if (n < 2) return;

    for (size_t i = 0; i + 1 < n; ++i) {
      const Proxy& A = _proxies[i];

      for (size_t j = i + 1; j < n; ++j) {
        const Proxy& B = _proxies[j];

        // Check masks
        const bool a2b = (A.layerMask & B.collidesWithMask) != 0;
        const bool b2a = (B.layerMask & A.collidesWithMask) != 0;
        if (!a2b && !b2a) {
          continue;
        }

        // static-static skip
        if (!A.hasVel && !B.hasVel) {
          continue;
        }

        // AABB test
        const bool overlap =
            (A.max.x > B.min.x) && (A.min.x < B.max.x) &&
            (A.max.y > B.min.y) && (A.min.y < B.max.y);
        if (!overlap) {
          continue;
        }

        // min-separation axis
        const float ox = std::min(A.max.x - B.min.x, B.max.x - A.min.x);
        const float oy = std::min(A.max.y - B.min.y, B.max.y - A.min.y);

        float nx = 0.0f, ny = 0.0f;
        if (ox < oy) {
          nx = (B.center.x >= A.center.x) ? +1.0f : -1.0f;
          ny = 0.0f;
        } else {
          nx = 0.0f;
          ny = (B.center.y >= A.center.y) ? +1.0f : -1.0f;
        }

        // emit only 1 pair -- keeping it (A, B)
        events::Physics2DCollision evt;
        evt.a = A.e;
        evt.b = B.e;
        evt.nx = nx;
        evt.ny = ny;

        _eventBus.emit(EVT_Physics2DCollision, evt);

        if (auto s = world.getComponent<ScriptComponent>(evt.a)) {
          auto instanceHandle = s->instance;
          auto instance = _scriptManager.getInstance(instanceHandle);
          if (instance != nullptr) {
            instance->onCollide(evt.b);
          }
        }
        if (auto s = world.getComponent<ScriptComponent>(evt.b)) {
          auto instanceHandle = s->instance;
          auto instance = _scriptManager.getInstance(instanceHandle);
          if (instance != nullptr) {
            instance->onCollide(evt.a);
          }
        }
      }
    }
  }

 private:
  struct Proxy {
    EntityId e;
    glm::vec2 center;
    glm::vec2 min;
    glm::vec2 max;
    uint32_t layerMask;
    uint32_t collidesWithMask;
    bool hasVel;
    glm::vec2 vel;  // only valid if hasVel
  };

  std::vector<Proxy> _proxies;

  EventBus& _eventBus;
  ScriptManager& _scriptManager;
};