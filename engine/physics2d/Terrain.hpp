#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "../core/ecs/World.hpp"
#include "../core/ecs/component/Component.hpp"
#include "TerrainChain.hpp"
#include "TransformComponent.hpp"

// Terrain: the ground of organic levels, as lines rather than tiles or boxes
// (hills, slopes, ledges drawn over painted art). Lines, not areas: a closed
// shape is its outline. A one-way line holds only from above (+y), a
// platform to jump up through.

// The layers terrain is on unless it says otherwise: all of them, so it's ground
// to everything; narrow its collisionLayer to let something pass.
inline constexpr uint32_t kTerrainLayers = 0xFFFFFFFFu;

// An entity's ground: written in a scene, or, on a tile map's entity, the
// map's drawn ground (kept in step with the map by the tile map module).
struct GroundComponent : public Component<GroundComponent> {
  COMPONENT_NAME("GroundComponent");
  std::vector<TerrainChain> chains;
  uint32_t collisionLayer = kTerrainLayers;
  glm::vec4 strokeColor{0.0f};  // drawn as lines this color (alpha 0: not drawn, art shows it)
  float strokeWidth = 2.0f;
};

struct TerrainSegment {
  EntityId entity;  // whose ground it is
  glm::vec2 a, b;   // world positions
  bool oneWay;
};

// visit(const TerrainSegment&) for each of `entity`'s terrain segments, if on
// mask's layers, that may be within the box (min, max).
template <typename Visit>
void forEachTerrainSegmentOf(World& world, EntityId entity, glm::vec2 min, glm::vec2 max, uint32_t mask, Visit visit) {
  const auto* trans = world.getComponent<TransformComponent>(entity);
  const auto* terrain = world.getComponent<GroundComponent>(entity);
  if (!trans || !terrain || !(terrain->collisionLayer & mask) || world.isPendingDestroy(entity)) return;
  const glm::vec2 at(trans->position);
  for (const TerrainChain& chain : terrain->chains) {
    if (glm::any(glm::lessThan(at + chain.max(), min)) || glm::any(glm::greaterThan(at + chain.min(), max))) continue;
    chain.forEachSegment([&](glm::vec2 a, glm::vec2 b) { visit(TerrainSegment{entity, at + a, at + b, chain.oneWay()}); });
  }
}

// The same for every entity's terrain, in world order. Entities about to be
// destroyed have none.
template <typename Visit>
void forEachTerrainSegment(World& world, glm::vec2 min, glm::vec2 max, uint32_t mask, Visit visit) {
  for (auto [entity, trans, terrain] : world.view<TransformComponent, GroundComponent>())
    forEachTerrainSegmentOf(world, entity, min, max, mask, visit);
}
