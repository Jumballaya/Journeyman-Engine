---
name: tiled
description: Paint and change tile maps in Tiled for a Journeyman game - maps, tilesets, ground and spawn points - and check them in the game. Use when the person wants a new map or level layout, or works in Tiled.
---

# Maps with Tiled

Journeyman reads Tiled's own JSON files: `*.tmj` maps and `*.tsj` tilesets.
The person paints in Tiled (or the editor); you edit the same files.

- **A map in a scene:** an entity with `"TileMapComponent": {"map": "assets/maps/town.tmj"}`.
  Tile (0, 0) is the bottom-left; the entity sits at the map's bottom-left.
- **What tiles mean** comes from the tileset's per-tile fields: the *Class*
  (scripts: `map.at(x, y) == "water"`), `solid` (blocks bodies), other bools
  as tags (`map.is(x, y, "deadly")`), animations and terrains.
- **Ground and things:** object layers. Polylines or polygons of class
  `ground` (solid) or `platform` (one-way) are ground; other objects can be
  spawn points your scripts read.
- **Open it for the person:** they keep Tiled open on the `.tmj`; it reloads
  when you change the file. Save in Tiled before you edit, or ask them to.
- **Check it:** `build`, then `drive_start` with `gl: true`, `drive` with
  `debug physics on`, `drive_frame` to see the ground and colliders.

`jm docs content`, *Tile maps*, has every field.
