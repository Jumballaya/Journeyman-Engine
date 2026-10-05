#pragma once

#include "../core/ecs/component/Component.hpp"
#include "TileGrid.hpp"

// {"tileset": "assets/maps/town.tileset.json" | {...}, "vars": {"theme": "over_"},
//  "rows": ["...", "..."] | "assets/maps/town.txt", "tileSize": 16,
//  "outside": "#" | {"left", "right", "top", "bottom"}}
// Tile (0, 0) sits at the entity's position (the map's bottom-left corner).
struct TileMapComponent : Component<TileMapComponent> {
  COMPONENT_NAME("TileMapComponent");
  TileGrid grid;
};
