#pragma once

#include "../core/ecs/component/Component.hpp"
#include "TileGrid.hpp"

// {"map": "assets/maps/town.tmj"} (a Tiled JSON map). The editor may instead
// pass the map itself, {"map": {...}, "mapPath": "assets/maps/town.tmj",
// "tilesets": {"assets/maps/town.tsj": {...}}}, to show unsaved edits.
// Tile (0, 0) sits at the entity's position (the map's bottom-left corner).
struct TileMapComponent : Component<TileMapComponent> {
  COMPONENT_NAME("TileMapComponent");
  TileGrid grid;
};
