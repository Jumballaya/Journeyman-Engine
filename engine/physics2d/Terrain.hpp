#pragma once

#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "../core/ecs/World.hpp"
#include "../core/ecs/component/Component.hpp"

// Terrain: the ground of organic levels, as lines rather than tiles or boxes
// (hills, slopes, ledges drawn over painted art). Queries hit it; movers stand
// on it. A one-way segment holds only from above (+y): a platform to jump up
// through.
struct TerrainSegment {
  EntityId entity;  // whose ground it is
  glm::vec2 a, b;   // world positions
  bool oneWay;
  uint32_t layerMask;
};

using TerrainVisitor = std::function<void(const TerrainSegment&)>;

// Where terrain comes from besides TerrainComponents: each source visits the
// segments a world's components hold (tile maps' drawn ground). Setting one
// by a name already used replaces it.
using TerrainSource = std::function<void(World&, const TerrainVisitor&)>;
void setTerrainSource(const std::string& name, TerrainSource source);

// Every terrain segment in the world: TerrainComponents' in world order, then
// each source's.
void forEachTerrainSegment(World& world, const TerrainVisitor& visit);

// Terrain written in a scene: lines through points, relative to the entity.
struct TerrainChain {
  std::vector<glm::vec2> points;
  bool closed = false;  // the last point joins the first (a polygon)
  bool oneWay = false;
};

struct TerrainComponent : public Component<TerrainComponent> {
  COMPONENT_NAME("TerrainComponent");
  std::vector<TerrainChain> chains;
  uint32_t layerMask = 1u << 0;
};
