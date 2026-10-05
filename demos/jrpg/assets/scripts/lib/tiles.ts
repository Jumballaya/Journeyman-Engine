// A map's tile grid: what blocks walking, and tile <-> world conversions.
// Tile (0, 0) is the bottom-left; world units are pixels.
import { GameMap } from "./maps";

export const TILE: f32 = 16;
// People are 16x24: their sprite sits this far above their feet's tile center.
export const FIGURE_LIFT: f32 = 6;

export class TileMap {
  constructor(readonly map: GameMap) {}

  get width(): i32 { return this.map.width; }
  get height(): i32 { return this.map.height; }

  // The map character at a tile; "T" (a tree) outside the map.
  at(tx: i32, ty: i32): string {
    if (tx < 0 || tx >= this.width || ty < 0 || ty >= this.height) return "T";
    return this.map.rows[this.height - 1 - ty].charAt(tx);
  }

  solid(tx: i32, ty: i32): bool { return "TrRwndF~cSLEIMK".includes(this.at(tx, ty)); }

  static tileOf(world: f32): i32 { return <i32>Mathf.floor(world / TILE); }
  static center(tile: i32): f32 { return (<f32>tile + 0.5) * TILE; }
}
