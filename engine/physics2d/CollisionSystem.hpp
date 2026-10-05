#pragma once

#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../core/ecs/World.hpp"
#include "../core/ecs/system/System.hpp"
#include "../core/scripting/ScriptManager.hpp"
#include "BoxColliderComponent.hpp"
#include "TransformComponent.hpp"
#include "VelocityComponent.hpp"

// Reports overlapping colliders to scripts (onCollide next update) when either's
// layerMask meets the other's collidesWithMask. A body counts as moving once it has
// a VelocityComponent or has ever changed position; two that never move never collide.
class CollisionSystem : public System {
 public:
  explicit CollisionSystem(ScriptManager& scripts) : _scripts(scripts) {}

  void update(World& world, float dt) override {
    if (!std::isfinite(dt) || dt <= 0.0f) return;  // paused: nothing moved

    _proxies.clear();
    std::unordered_map<EntityId, glm::vec2> centers;
    std::unordered_set<EntityId> moved;
    for (auto [entity, trans, collider] : world.view<TransformComponent, BoxColliderComponent>()) {
      if (world.isPendingDestroy(entity)) continue;
      const glm::vec2 center = glm::vec2(trans->position) + collider->offset;
      centers[entity] = center;
      auto last = _lastCenters.find(entity);
      const bool moves = world.hasComponent<VelocityComponent>(entity) || _moved.contains(entity) ||
                         (last != _lastCenters.end() && last->second != center);
      if (moves) moved.insert(entity);
      _proxies.push_back(Proxy{entity, center - collider->halfExtents, center + collider->halfExtents,
                               collider->layerMask, collider->collidesWithMask, moves});
    }
    _lastCenters = std::move(centers);  // also forgets destroyed entities
    _moved = std::move(moved);

    for (size_t i = 0; i + 1 < _proxies.size(); ++i) {
      const Proxy& a = _proxies[i];
      for (size_t j = i + 1; j < _proxies.size(); ++j) {
        const Proxy& b = _proxies[j];
        const bool interested = (a.layerMask & b.collidesWithMask) || (b.layerMask & a.collidesWithMask);
        const bool overlap = a.max.x > b.min.x && a.min.x < b.max.x && a.max.y > b.min.y && a.min.y < b.max.y;
        if (interested && (a.moves || b.moves) && overlap) _scripts.queueCollision(a.entity, b.entity);
      }
    }
  }

  const char* name() const override { return "CollisionSystem"; }

 private:
  struct Proxy {
    EntityId entity;
    glm::vec2 min, max;
    uint32_t layerMask, collidesWithMask;
    bool moves;
  };

  ScriptManager& _scripts;
  std::vector<Proxy> _proxies;
  std::unordered_map<EntityId, glm::vec2> _lastCenters;
  std::unordered_set<EntityId> _moved;
};
