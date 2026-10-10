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

// The layers terrain is on unless it says otherwise.
inline constexpr uint32_t kTerrainLayer = 1u << 0;

// An entity's ground: written in a scene, or, on a tile map's entity, the
// map's drawn ground (kept in step with the map by the tile map module).
struct TerrainComponent : public Component<TerrainComponent> {
  COMPONENT_NAME("TerrainComponent");
  std::vector<TerrainChain> chains;
  uint32_t layerMask = kTerrainLayer;
};

struct TerrainSegment {
  EntityId entity;  // whose ground it is
  glm::vec2 a, b;   // world positions
  bool oneWay;
  uint32_t layerMask;
};

// visit(const TerrainSegment&) for each terrain segment on mask's layers that
// may be within the box (min, max), in world order. Entities about to be
// destroyed have none.
template <typename Visit>
void forEachTerrainSegment(World& world, glm::vec2 min, glm::vec2 max, uint32_t mask, Visit visit);

template <typename Visit>
void forEachTerrainSegment(World& world, glm::vec2 min, glm::vec2 max, uint32_t mask, Visit visit) {
  for (auto [entity, trans, terrain] : world.view<TransformComponent, TerrainComponent>()) {
    if (!(terrain->layerMask & mask) || world.isPendingDestroy(entity)) continue;
    const glm::vec2 at(trans->position);
    for (const TerrainChain& chain : terrain->chains) {
      if (glm::any(glm::lessThan(at + chain.max(), min)) || glm::any(glm::greaterThan(at + chain.min(), max))) continue;
      chain.forEachSegment([&](glm::vec2 a, glm::vec2 b) { visit(TerrainSegment{entity, at + a, at + b, chain.oneWay(), terrain->layerMask}); });
    }
  }
}
