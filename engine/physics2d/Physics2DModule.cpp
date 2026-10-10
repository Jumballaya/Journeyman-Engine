#include "Physics2DModule.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "../core/app/Engine.hpp"
#include "../core/app/Registration.hpp"
#include "../core/logger/logging.hpp"
#include "Blocking.hpp"
#include "BoxColliderComponent.hpp"
#include "CircleColliderComponent.hpp"
#include "LifetimeComponent.hpp"
#include "Queries.hpp"
#include "ScrollWrapComponent.hpp"
#include "Terrain.hpp"
#include "TransformComponent.hpp"
#include "Systems.hpp"
#include "TransformHierarchy.hpp"
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

void Physics2DModule::registerComponents(Engine& app) {
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
                 {FieldSchema::vec3("position", 0, 0, 0, "Where it is (a child's: from its parent); z orders drawing (higher is in front)"),
                  FieldSchema::vec2("scale", 1, 1, "Half size in pixels for sprites (32 = a 64 px quad)"),
                  FieldSchema::angle("rotation", "Counter-clockwise")}},
  });

  world.registerComponent<VelocityComponent>({
      .fromJson = [](VelocityComponent& c, const nlohmann::json& json, EntityId) {
        std::array<float, 2> v;
        if (readArray(json, "velocity", v)) c.velocity = {v[0], v[1]};
        if (readArray(json, "acceleration", v)) c.acceleration = {v[0], v[1]};
        const std::string motion = json.value("motion", std::string("free"));
        c.motion = motion == "move" ? kMoveMotion : motion == "walk" ? kWalkMotion : kFreeMotion;
      },
      .scriptFields = {
          scriptField<VelocityComponent>("vx", [](VelocityComponent& c) -> float& { return c.velocity.x; }),
          scriptField<VelocityComponent>("vy", [](VelocityComponent& c) -> float& { return c.velocity.y; }),
          scriptField<VelocityComponent>("ax", [](VelocityComponent& c) -> float& { return c.acceleration.x; }),
          scriptField<VelocityComponent>("ay", [](VelocityComponent& c) -> float& { return c.acceleration.y; }),
          scriptField<VelocityComponent>("motion", [](VelocityComponent& c) -> uint32_t& { return c.motion; }),
          scriptField<VelocityComponent>("dropThrough", [](VelocityComponent& c) -> uint32_t& { return c.dropThrough; }),
          scriptField<VelocityComponent>("blockedX", [](VelocityComponent& c) -> float& { return c.blocked.x; }),
          scriptField<VelocityComponent>("blockedY", [](VelocityComponent& c) -> float& { return c.blocked.y; }),
      },
      .schema = {"Velocity", "Physics", "Moves the entity every frame",
                 {FieldSchema::vec2("velocity", 0, 0, "Pixels per second"),
                  FieldSchema::vec2("acceleration", 0, 0, "Pixels per second, per second (gravity)"),
                  FieldSchema::choice("motion", {"free", "move", "walk"},
                                      "free: through everything; move/walk: through solids and drawn ground like "
                                      "entity.move()/walk() (needs a box collider, or terrain: moving ground)")}},
  });

  world.registerComponent<BoxColliderComponent>({
      .fromJson = [](BoxColliderComponent& c, const nlohmann::json& json, EntityId) {
        std::array<float, 2> v;
        if (readArray(json, "size", v)) c.halfExtents = {v[0], v[1]};  // legacy alias
        if (readArray(json, "halfExtents", v)) c.halfExtents = {v[0], v[1]};
        if (readArray(json, "offset", v)) c.offset = {v[0], v[1]};
        c.layerMask = readMask(json, "layerMask", c.layerMask);
        c.collidesWithMask = readMask(json, "collidesWithMask", c.collidesWithMask);
        c.blocksMask = readMask(json, "blocksMask", c.blocksMask);
      },
      .scriptFields = {
          scriptField<BoxColliderComponent>("halfWidth", [](BoxColliderComponent& c) -> float& { return c.halfExtents.x; }),
          scriptField<BoxColliderComponent>("halfHeight", [](BoxColliderComponent& c) -> float& { return c.halfExtents.y; }),
          scriptField<BoxColliderComponent>("offsetX", [](BoxColliderComponent& c) -> float& { return c.offset.x; }),
          scriptField<BoxColliderComponent>("offsetY", [](BoxColliderComponent& c) -> float& { return c.offset.y; }),
          scriptField<BoxColliderComponent>("layerMask", [](BoxColliderComponent& c) -> uint32_t& { return c.layerMask; }),
          scriptField<BoxColliderComponent>("collidesWithMask", [](BoxColliderComponent& c) -> uint32_t& { return c.collidesWithMask; }),
          scriptField<BoxColliderComponent>("blocksMask", [](BoxColliderComponent& c) -> uint32_t& { return c.blocksMask; }),
      },
      .schema = {"Box Collider", "Physics", "Reports overlaps to scripts (onCollide); solid to movers with blocksMask",
                 {FieldSchema::vec2("halfExtents", 8, 8, "Half width and height, from the center"),
                  FieldSchema::vec2("offset", 0, 0, "From the transform's position"),
                  FieldSchema::mask("layerMask", 1, "Layers this collider is on"),
                  FieldSchema::mask("collidesWithMask", 0xFFFFFFFFu, "Layers it wants to touch (a pair collides when either side wants the other)"),
                  FieldSchema::mask("blocksMask", 0, "Layers it's solid to: entities on them moving with move() stop at it")}},
  });

  world.registerComponent<CircleColliderComponent>({
      .fromJson = [](CircleColliderComponent& c, const nlohmann::json& json, EntityId) {
        if (json.contains("radius") && json["radius"].is_number()) c.radius = std::max(0.0f, json["radius"].get<float>());
        std::array<float, 2> v;
        if (readArray(json, "offset", v)) c.offset = {v[0], v[1]};
        c.layerMask = readMask(json, "layerMask", c.layerMask);
        c.collidesWithMask = readMask(json, "collidesWithMask", c.collidesWithMask);
      },
      .scriptFields = {
          scriptField<CircleColliderComponent>("radius", [](CircleColliderComponent& c) -> float& { return c.radius; }),
          scriptField<CircleColliderComponent>("offsetX", [](CircleColliderComponent& c) -> float& { return c.offset.x; }),
          scriptField<CircleColliderComponent>("offsetY", [](CircleColliderComponent& c) -> float& { return c.offset.y; }),
          scriptField<CircleColliderComponent>("layerMask", [](CircleColliderComponent& c) -> uint32_t& { return c.layerMask; }),
          scriptField<CircleColliderComponent>("collidesWithMask",
                                               [](CircleColliderComponent& c) -> uint32_t& { return c.collidesWithMask; }),
      },
      .schema = {"Circle Collider", "Physics", "A round collider: reports overlaps to scripts (onCollide); not solid",
                 {FieldSchema::number("radius", 8, "From the center", 0),
                  FieldSchema::vec2("offset", 0, 0, "From the transform's position"),
                  FieldSchema::mask("layerMask", 1, "Layers this collider is on"),
                  FieldSchema::mask("collidesWithMask", 0xFFFFFFFFu, "Layers it wants to touch (a pair collides when either side wants the other)")}},
  });

  world.registerComponent<TerrainComponent>({
      .fromJson = [](TerrainComponent& c, const nlohmann::json& json, EntityId) {
        c.chains.clear();
        const nlohmann::json chains = json.value("chains", nlohmann::json::array());
        for (const auto& chain : chains.is_array() ? chains : nlohmann::json::array({chains})) {
          const nlohmann::json points = chain.is_object() ? chain.value("points", nlohmann::json()) : nlohmann::json();
          std::vector<glm::vec2> at;
          bool wellFormed = points.is_array();
          for (const auto& p : wellFormed ? points : nlohmann::json::array()) {
            wellFormed = wellFormed && p.is_array() && p.size() == 2 && p[0].is_number() && p[1].is_number() &&
                         std::isfinite(p[0].get<float>()) && std::isfinite(p[1].get<float>());
            if (wellFormed) at.emplace_back(p[0].get<float>(), p[1].get<float>());
          }
          if (!wellFormed) {
            JM_LOG_ERROR("[Physics2D] TerrainComponent: a chain isn't {{\"points\": [[x, y], ...]}}: {}", chain.dump());
            continue;
          }
          c.chains.emplace_back(std::move(at), chain.value("closed", false), chain.value("oneWay", false));
        }
        c.layerMask = readMask(json, "layerMask", c.layerMask);
      },
      .schema = {"Terrain", "Physics", "Ground as lines (slopes, hills, ledges) that rays and overlaps hit",
                 {FieldSchema::json("chains", "Lines: [{\"points\": [[x, y], ...], \"closed\": false, \"oneWay\": false}], relative to the entity"),
                  FieldSchema::mask("layerMask", kTerrainLayer, "Layers it's on (what queries' masks match)")}},
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

  registerLocalTransform(world);  // a child's place under its parent
}

void Physics2DModule::initialize(Engine& app) {
  World& world = app.getWorld();
  world.registerSystem<MovementSystem>();
  world.registerSystem<LifetimeSystem>();
  world.registerSystem<ScrollWrapSystem>();
  installTransformHierarchy(world);  // after movement: children follow where their parents went
  ScriptManager& scripts = app.getScriptManager();
  world.registerSystem<CollisionSystem>([&scripts](EntityId a, EntityId b) { scripts.queueCollision(a, b); });
}

void Physics2DModule::bindScriptApi(Engine& app) {
  World& world = app.getWorld();
  // Moves (or walks) an entity against solid colliders and terrain; writes a MoveOut.
  const auto report = [](const BlockedMove& m, host::WasmBytes out) {
    struct MoveOut {
      int32_t hitX, hitY;
      uint32_t byXIndex, byXGeneration, byYIndex, byYGeneration;
      float normalX, normalY;
    };
    static_assert(sizeof(MoveOut) == 32, "entity.ts reads these 32 bytes");
    const MoveOut r{m.hit.x, m.hit.y, m.hitX.index, m.hitX.generation, m.hitY.index, m.hitY.generation, m.normal.x, m.normal.y};
    if (out.size >= sizeof(r)) std::memcpy(out.data, &r, sizeof(r));
  };
  app.getScriptManager().bind("__jmPhysicsMove", [&world, report](EntityId id, float dx, float dy, float slide, host::WasmBytes out) {
    report(moveBlocked(world, id, {dx, dy}, slide), out);
  });
  app.getScriptManager().bind("__jmPhysicsWalk", [&world, report](EntityId id, float dx, float dy, int32_t dropThrough,
                                                                  host::WasmBytes out) {
    report(walkBlocked(world, id, {dx, dy}, dropThrough != 0), out);
  });
  // The first collider on mask's layers along a ray, skipping `ignore`:
  // writes a RaycastOut; returns whether there was one.
  app.getScriptManager().bind("__jmPhysicsRaycast", [&world](float x, float y, float dx, float dy, float distance, uint32_t mask,
                                                             EntityId ignore, host::WasmBytes out) {
    struct RaycastOut {
      uint32_t index, generation;
      float x, y, normalX, normalY, distance;
    };
    static_assert(sizeof(RaycastOut) == 28, "physics.ts reads these 28 bytes");
    const auto hit = raycast(world, {x, y}, {dx, dy}, distance, mask, ignore);
    if (!hit || out.size < sizeof(RaycastOut)) return 0;
    const RaycastOut r{hit->entity.index, hit->entity.generation, hit->point.x, hit->point.y,
                       hit->normal.x, hit->normal.y, hit->distance};
    std::memcpy(out.data, &r, sizeof(r));
    return 1;
  });
  // The colliders on mask's layers overlapping a box (kind 0; half size) or a
  // circle (kind 1; radius), skipping `ignore`: writes (index, generation)
  // pairs while they fit; returns how many there are.
  app.getScriptManager().bind("__jmPhysicsOverlap", [&world](int32_t kind, float x, float y, float halfWidth, float halfHeight,
                                                             float radius, uint32_t mask, EntityId ignore, host::WasmBytes out) {
    const Shape area = kind == 1 ? Shape::circle({x, y}, radius)
                                 : Shape::box({x, y}, glm::max(glm::vec2(halfWidth, halfHeight), glm::vec2(0.0f)));
    const auto found = overlapping(world, area, mask, ignore);
    for (size_t i = 0; i < found.size() && (i + 1) * 8 <= out.size; ++i) {
      std::memcpy(out.data + i * 8, &found[i].index, 4);
      std::memcpy(out.data + i * 8 + 4, &found[i].generation, 4);
    }
    return static_cast<int32_t>(found.size());
  });
}
