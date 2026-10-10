#include "TileMapTerrainSystem.hpp"

#include <vector>

void TileMapTerrainSystem::update(World& world, float) {
  // Changing components moves rows: gather first, change after.
  std::vector<EntityId> stale, gone;
  for (auto [entity, map] : world.view<TileMapComponent>()) {
    if (auto it = _synced.find(entity); it == _synced.end() || it->second != map->grid.revision()) stale.push_back(entity);
  }
  for (const auto& [entity, revision] : _synced) {
    if (!world.hasComponent<TileMapComponent>(entity)) gone.push_back(entity);
  }
  for (const EntityId entity : gone) {
    if (world.hasComponent<TerrainComponent>(entity)) world.removeComponent<TerrainComponent>(entity);
    _synced.erase(entity);
  }
  for (const EntityId entity : stale) {
    if (!world.hasComponent<TerrainComponent>(entity)) world.addComponent<TerrainComponent>(entity);
    const TileGrid& grid = world.getComponent<TileMapComponent>(entity)->grid;  // after adding: that moved its row
    world.getComponent<TerrainComponent>(entity)->chains = grid.terrain();
    _synced[entity] = grid.revision();
  }
}
