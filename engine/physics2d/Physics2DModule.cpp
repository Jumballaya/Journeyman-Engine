#include "Physics2DModule.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <vector>

#include "../core/app/Engine.hpp"
#include "../core/app/Registration.hpp"
#include "../core/ecs/system/SystemTraits.hpp"
#include "BoxColliderComponent.hpp"
#include "LifetimeComponent.hpp"
#include "ScrollWrapComponent.hpp"
#include "TransformComponent.hpp"
#include "VelocityComponent.hpp"

REGISTER_MODULE(Physics2DModule);

namespace {

// Reads a JSON array of N numbers into `out` when present and well-formed.
template <size_t N>
bool readArray(const nlohmann::json& json, const char* key, std::array<float, N>& out) {
  if (!json.contains(key) || !json[key].is_array() || json[key].size() != N) return false;
  out = json[key].get<std::array<float, N>>();
  return true;
}

uint32_t readMask(const nlohmann::json& json, const char* key, uint32_t fallback) {
  return json.contains(key) && json[key].is_number_unsigned() ? json[key].get<uint32_t>() : fallback;
}

class MovementSystem : public System {
 public:
  void update(World& world, float dt) override {
    constexpr float kMaxDt = 1.0f / 20.0f;
    dt = std::isfinite(dt) ? std::clamp(dt, 0.0f, kMaxDt) : 0.0f;
    for (auto [entity, trans, vel] : world.view<TransformComponent, VelocityComponent>()) {
      vel->velocity += vel->acceleration * dt;
      trans->position.x += vel->velocity.x * dt;
      trans->position.y += vel->velocity.y * dt;
    }
  }
  const char* name() const override { return "MovementSystem"; }
};

class LifetimeSystem : public System {
 public:
  void update(World& world, float dt) override {
    for (auto [entity, life] : world.view<LifetimeComponent>()) {
      life->seconds -= dt;
      if (life->seconds <= 0.0f) world.destroyDeferred(entity);
    }
  }
  const char* name() const override { return "LifetimeSystem"; }
};

class ScrollWrapSystem : public System {
 public:
  void update(World& world, float) override {
    for (auto [entity, wrap, trans] : world.view<ScrollWrapComponent, TransformComponent>()) {
      const float span = wrap->maxY - wrap->minY;
      if (span <= 0.0f) continue;
      float& y = trans->position.y;
      if (y < wrap->minY) y += span * std::ceil((wrap->minY - y) / span);
      if (y > wrap->maxY) y -= span * std::ceil((y - wrap->maxY) / span);
    }
  }
  const char* name() const override { return "ScrollWrapSystem"; }
};

// Reports overlapping colliders to scripts (onCollide next update) when either's
// layerMask meets the other's collidesWithMask. A body counts as moving once it has
// a VelocityComponent or has ever changed position; two that never move never collide.
class CollisionSystem : public System {
 public:
  explicit CollisionSystem(ScriptManager& scripts) : _scripts(scripts) {}

  void update(World& world, float dt) override {
    if (!std::isfinite(dt) || dt <= 0.0f) return;  // paused: nothing moved

    _proxies.clear();
    std::unordered_map<EntityId, Body> bodies;
    for (auto [entity, trans, collider] : world.view<TransformComponent, BoxColliderComponent>()) {
      if (world.isPendingDestroy(entity)) continue;
      const glm::vec2 center = glm::vec2(trans->position) + collider->offset;
      auto last = _bodies.find(entity);
      const bool moves = world.hasComponent<VelocityComponent>(entity) ||
                         (last != _bodies.end() && (last->second.moves || last->second.center != center));
      bodies[entity] = {center, moves};
      _proxies.push_back(Proxy{entity, center - collider->halfExtents, center + collider->halfExtents,
                               collider->layerMask, collider->collidesWithMask, moves});
    }
    _bodies = std::move(bodies);  // also forgets destroyed entities

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
  struct Body {
    glm::vec2 center;
    bool moves;
  };
  struct Proxy {
    EntityId entity;
    glm::vec2 min, max;
    uint32_t layerMask, collidesWithMask;
    bool moves;
  };

  ScriptManager& _scripts;
  std::vector<Proxy> _proxies;
  std::unordered_map<EntityId, Body> _bodies;
};

struct Physics2D_Moved {};  // provided by MovementSystem

}  // namespace

template <>
struct SystemTraits<MovementSystem> {
  using DependsOn = EmptyList;
  using Provides = TypeList<Physics2D_Moved>;
  using Reads = TypeList<VelocityComponent, TransformComponent>;
  using Writes = TypeList<TransformComponent>;
  static constexpr SystemStage stage = SystemStage::Physics;
};

template <>
struct SystemTraits<CollisionSystem> {
  using DependsOn = TypeList<Physics2D_Moved>;
  using Provides = EmptyList;
  using Reads = TypeList<TransformComponent, BoxColliderComponent, VelocityComponent>;
  using Writes = EmptyList;
  static constexpr SystemStage stage = SystemStage::PostPhysics;
};

template <>
struct SystemTraits<LifetimeSystem> {
  using DependsOn = EmptyList;
  using Provides = EmptyList;
  using Reads = EmptyList;
  using Writes = TypeList<LifetimeComponent>;
  static constexpr SystemStage stage = SystemStage::Physics;
};

template <>
struct SystemTraits<ScrollWrapSystem> {
  using DependsOn = TypeList<Physics2D_Moved>;
  using Provides = EmptyList;
  using Reads = TypeList<ScrollWrapComponent>;
  using Writes = TypeList<TransformComponent>;
  static constexpr SystemStage stage = SystemStage::Physics;
};

void Physics2DModule::initialize(Engine& app) {
  World& world = app.getWorld();

  world.registerComponent<TransformComponent>({
      .fromJson = [](TransformComponent& c, const nlohmann::json& json, EntityId) {
        std::array<float, 3> position;
        std::array<float, 2> scale;
        if (readArray(json, "position", position)) c.position = {position[0], position[1], position[2]};
        if (readArray(json, "scale", scale)) c.scale = {scale[0], scale[1]};
        c.rotationRad = json.value("rotation", c.rotationRad);
      },
      .scriptFields = {
          scriptField<TransformComponent>("x", [](TransformComponent& c) -> float& { return c.position.x; }),
          scriptField<TransformComponent>("y", [](TransformComponent& c) -> float& { return c.position.y; }),
          scriptField<TransformComponent>("z", [](TransformComponent& c) -> float& { return c.position.z; }),
          scriptField<TransformComponent>("scaleX", [](TransformComponent& c) -> float& { return c.scale.x; }),
          scriptField<TransformComponent>("scaleY", [](TransformComponent& c) -> float& { return c.scale.y; }),
          scriptField<TransformComponent>("rotation", [](TransformComponent& c) -> float& { return c.rotationRad; }),
      },
      .schema = {"Transform", "Core", "Position, scale and rotation in the world",
                 {FieldSchema::vec3("position", 0, 0, 0, "World position; z orders drawing (higher is in front)"),
                  FieldSchema::vec2("scale", 1, 1, "Half size in pixels for sprites (32 = a 64 px quad)"),
                  FieldSchema::angle("rotation", "Counter-clockwise")}},
  });

  world.registerComponent<VelocityComponent>({
      .fromJson = [](VelocityComponent& c, const nlohmann::json& json, EntityId) {
        std::array<float, 2> v;
        if (readArray(json, "velocity", v)) c.velocity = {v[0], v[1]};
        if (readArray(json, "acceleration", v)) c.acceleration = {v[0], v[1]};
      },
      .scriptFields = {
          scriptField<VelocityComponent>("vx", [](VelocityComponent& c) -> float& { return c.velocity.x; }),
          scriptField<VelocityComponent>("vy", [](VelocityComponent& c) -> float& { return c.velocity.y; }),
          scriptField<VelocityComponent>("ax", [](VelocityComponent& c) -> float& { return c.acceleration.x; }),
          scriptField<VelocityComponent>("ay", [](VelocityComponent& c) -> float& { return c.acceleration.y; }),
      },
      .schema = {"Velocity", "Physics", "Moves the entity every frame",
                 {FieldSchema::vec2("velocity", 0, 0, "Pixels per second"),
                  FieldSchema::vec2("acceleration", 0, 0, "Pixels per second, per second (gravity)")}},
  });

  world.registerComponent<BoxColliderComponent>({
      .fromJson = [](BoxColliderComponent& c, const nlohmann::json& json, EntityId) {
        std::array<float, 2> v;
        if (readArray(json, "size", v)) c.halfExtents = {v[0], v[1]};  // legacy alias
        if (readArray(json, "halfExtents", v)) c.halfExtents = {v[0], v[1]};
        if (readArray(json, "offset", v)) c.offset = {v[0], v[1]};
        c.layerMask = readMask(json, "layerMask", c.layerMask);
        c.collidesWithMask = readMask(json, "collidesWithMask", c.collidesWithMask);
      },
      .scriptFields = {
          scriptField<BoxColliderComponent>("halfWidth", [](BoxColliderComponent& c) -> float& { return c.halfExtents.x; }),
          scriptField<BoxColliderComponent>("halfHeight", [](BoxColliderComponent& c) -> float& { return c.halfExtents.y; }),
          scriptField<BoxColliderComponent>("offsetX", [](BoxColliderComponent& c) -> float& { return c.offset.x; }),
          scriptField<BoxColliderComponent>("offsetY", [](BoxColliderComponent& c) -> float& { return c.offset.y; }),
          scriptField<BoxColliderComponent>("layerMask", [](BoxColliderComponent& c) -> uint32_t& { return c.layerMask; }),
          scriptField<BoxColliderComponent>("collidesWithMask", [](BoxColliderComponent& c) -> uint32_t& { return c.collidesWithMask; }),
      },
      .schema = {"Box Collider", "Physics", "Reports overlaps to scripts (onCollide)",
                 {FieldSchema::vec2("halfExtents", 8, 8, "Half width and height, from the center"),
                  FieldSchema::vec2("offset", 0, 0, "From the transform's position"),
                  FieldSchema::mask("layerMask", 1, "Layers this collider is on"),
                  FieldSchema::mask("collidesWithMask", 0xFFFFFFFFu, "Layers it collides with")}},
  });

  world.registerComponent<LifetimeComponent>({
      .fromJson = [](LifetimeComponent& c, const nlohmann::json& json, EntityId) {
        c.seconds = json.value("seconds", c.seconds);
      },
      .scriptFields = {
          scriptField<LifetimeComponent>("seconds", [](LifetimeComponent& c) -> float& { return c.seconds; }),
      },
      .schema = {"Lifetime", "Physics", "Destroys the entity after a time",
                 {FieldSchema::number("seconds", 1, "Seconds until the entity is destroyed", 0, 0, 0.05f)}},
  });

  world.registerComponent<ScrollWrapComponent>({
      .fromJson = [](ScrollWrapComponent& c, const nlohmann::json& json, EntityId) {
        c.minY = json.value("minY", c.minY);
        c.maxY = json.value("maxY", c.maxY);
      },
      .schema = {"Scroll Wrap", "Physics", "Wraps vertically between two heights (scrolling backdrops)",
                 {FieldSchema::number("minY", 0, "Below this, jump up to maxY"),
                  FieldSchema::number("maxY", 0, "The wrap's top")}},
  });

  world.registerSystem<MovementSystem>();
  world.registerSystem<LifetimeSystem>();
  world.registerSystem<ScrollWrapSystem>();
  world.registerSystem<CollisionSystem>(app.getScriptManager());
}
