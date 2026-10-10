#include "TileMapTerrainSystem.hpp"

void TileMapTerrainSystem::update(World& world, float) {
  std::vector<EntityId> fresh;
  for (auto [entity, map] : world.view<TileMapComponent>()) {
    if (map->terrainSynced) continue;
    map->terrainSynced = true;
    fresh.push_back(entity);
  }
  // Adding components moves rows: not while viewing them.
  for (const EntityId entity : fresh) {
    if (!world.hasComponent<TerrainComponent>(entity)) world.addComponent<TerrainComponent>(entity);
    // Read after adding: that moved the map's row.
    world.getComponent<TerrainComponent>(entity)->chains = world.getComponent<TileMapComponent>(entity)->grid.terrain();
  }
}
