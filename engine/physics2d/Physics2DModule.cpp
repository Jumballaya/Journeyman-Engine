#include "Physics2DModule.hpp"

#include <array>

#include "../core/app/Engine.hpp"
#include "../core/app/Registration.hpp"
#include "BoxColliderComponent.hpp"
#include "CollisionSystem.hpp"
#include "LifetimeComponent.hpp"
#include "MovementSystem.hpp"
#include "ScrollWrapComponent.hpp"
#include "Traits.hpp"
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

}  // namespace

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
  });

  world.registerComponent<VelocityComponent>({
      .fromJson = [](VelocityComponent& c, const nlohmann::json& json, EntityId) {
        std::array<float, 2> velocity;
        if (readArray(json, "velocity", velocity)) c.velocity = {velocity[0], velocity[1]};
      },
      .scriptFields = {
          scriptField<VelocityComponent>("vx", [](VelocityComponent& c) -> float& { return c.velocity.x; }),
          scriptField<VelocityComponent>("vy", [](VelocityComponent& c) -> float& { return c.velocity.y; }),
      },
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
  });

  world.registerComponent<LifetimeComponent>({
      .fromJson = [](LifetimeComponent& c, const nlohmann::json& json, EntityId) {
        c.seconds = json.value("seconds", c.seconds);
      },
      .scriptFields = {
          scriptField<LifetimeComponent>("seconds", [](LifetimeComponent& c) -> float& { return c.seconds; }),
      },
  });

  world.registerComponent<ScrollWrapComponent>({
      .fromJson = [](ScrollWrapComponent& c, const nlohmann::json& json, EntityId) {
        c.minY = json.value("minY", c.minY);
        c.maxY = json.value("maxY", c.maxY);
      },
  });

  world.registerSystem<MovementSystem>();
  world.registerSystem<LifetimeSystem>();
  world.registerSystem<ScrollWrapSystem>();
  world.registerSystem<CollisionSystem>(app.getScriptManager());
}

void Physics2DModule::shutdown(Engine&) {}
