#pragma once

#include "../core/ecs/system/System.hpp"
#include "../core/ecs/system/SystemTraits.hpp"
#include "../physics2d/Terrain.hpp"
#include "TileMapComponent.hpp"

// Gives each tile map's entity the map's drawn ground as its TerrainComponent,
// after the map is spawned or reloaded: first thing in the next frame.
class TileMapTerrainSystem : public System {
 public:
  void update(World& world, float) override;
  const char* name() const override { return "TileMapTerrainSystem"; }
};

template <>
struct SystemTraits<TileMapTerrainSystem> {
  using DependsOn = EmptyList;
  using Provides = EmptyList;
  using Reads = TypeList<TileMapComponent>;
  using Writes = TypeList<TerrainComponent>;
  static constexpr SystemStage stage = SystemStage::Input;
};
