#pragma once

#include <cstdint>
#include <unordered_map>

#include "../core/ecs/system/System.hpp"
#include "../core/ecs/system/SystemTraits.hpp"
#include "../physics2d/Terrain.hpp"
#include "TileMapComponent.hpp"

// Gives each tile map's entity the map's drawn ground as its GroundComponent
// (replacing any it had), first thing in the frame after the map is spawned or
// another is loaded; takes it away again with the map.
class TileMapTerrainSystem : public System {
 public:
  void update(World& world, float) override;
  const char* name() const override { return "TileMapTerrainSystem"; }

 private:
  std::unordered_map<EntityId, uint64_t> _synced;  // map entity -> the grid revision its terrain is
};

template <>
struct SystemTraits<TileMapTerrainSystem> {
  using DependsOn = EmptyList;
  using Provides = EmptyList;
  using Reads = TypeList<TileMapComponent>;
  using Writes = TypeList<GroundComponent>;
  static constexpr SystemStage stage = SystemStage::Input;
};
