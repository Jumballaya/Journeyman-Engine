#include "Terrain.hpp"

#include <map>

#include "TransformComponent.hpp"


namespace {

// By name, so each kind of terrain is visited once however often a module
// sets it up (every engine an editor hosts does).
std::map<std::string, TerrainSource>& sources() {
  static std::map<std::string, TerrainSource> all;
  return all;
}

}  // namespace

void setTerrainSource(const std::string& name, TerrainSource source) { sources()[name] = std::move(source); }

void forEachTerrainSegment(World& world, const TerrainVisitor& visit) {
  if (world.getComponentRegistry().getInfo(TerrainComponent::typeId())) {  // a world without physics has none
    for (auto [entity, trans, terrain] : world.view<TransformComponent, TerrainComponent>()) {
      if (world.isPendingDestroy(entity)) continue;
      const glm::vec2 at(trans->position);
      for (const TerrainChain& chain : terrain->chains) {
        const size_t n = chain.points.size();
        for (size_t i = 0; i + 1 < n + (chain.closed && n > 2 ? 1 : 0); ++i)
          visit({entity, at + chain.points[i], at + chain.points[(i + 1) % n], chain.oneWay, terrain->layerMask});
      }
    }
  }
  for (const auto& [name, source] : sources()) source(world, visit);
}
