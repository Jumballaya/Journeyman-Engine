// A level's tile grid: what is solid, deadly or bumpable at a tile, and
// tile <-> world conversions. Tile (0, 0) is the bottom-left; world units are pixels.
import { GameState } from "@jm/runtime";
import { Level } from "./levels";
import { Session } from "./session";

export const TILE: f32 = 16;

export class TileMap {
  constructor(readonly level: Level) {}

  get width(): i32 { return this.level.width; }
  get height(): i32 { return this.level.height; }

  // The map character at a tile; "." outside the map.
  at(tx: i32, ty: i32): string {
    if (tx < 0 || tx >= this.width || ty < 0 || ty >= this.height) return ".";
    return this.level.map[this.height - 1 - ty].charAt(tx);
  }

  // The level's sides are walls; above and below are open (falling out kills).
  solid(tx: i32, ty: i32): bool {
    if (tx < 0 || tx >= this.width) return true;
    const c = this.at(tx, ty);
    if (c == "B") return !GameState.has(Session.tileKey(tx, ty));  // broken bricks stay open
    return "#B?MX[]{}=".includes(c);
  }

  deadly(tx: i32, ty: i32): bool { return this.at(tx, ty) == "~"; }
  bumpable(tx: i32, ty: i32): bool { return "B?M".includes(this.at(tx, ty)) && this.solid(tx, ty); }

  static tileOf(world: f32): i32 { return <i32>Mathf.floor(world / TILE); }
  static center(tile: i32): f32 { return (<f32>tile + 0.5) * TILE; }
}

// Tag of a tile's sprite entity, so bumpers can find it.
export function tileTag(tx: i32, ty: i32): string {
  return "tile:" + tx.toString() + "," + ty.toString();
}
