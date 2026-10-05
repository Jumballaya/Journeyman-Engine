// An area's tile grid: what blocks movement, rooms, and tile <-> world
// conversions. Tile (0, 0) is the bottom-left; world units are pixels.
import { Area, ROOM_W, ROOM_H } from "./areas";
import { Session, placeKey } from "./session";

export const TILE: f32 = 16;

export class TileMap {
  constructor(readonly area: Area) {}

  get width(): i32 { return this.area.width; }
  get height(): i32 { return this.area.height; }

  // The map character at a tile; "#" outside the area.
  at(tx: i32, ty: i32): string {
    if (tx < 0 || tx >= this.width || ty < 0 || ty >= this.height) return "#";
    return this.area.map[this.height - 1 - ty].charAt(tx);
  }

  solid(tx: i32, ty: i32): bool {
    const c = this.at(tx, ty);
    if (c == "+") return !Session.done(placeKey(this.area.id, tx, ty));  // opened doors stay open
    return "#TRWFH".includes(c);
  }

  static tileOf(world: f32): i32 { return <i32>Mathf.floor(world / TILE); }
  static center(tile: i32): f32 { return (<f32>tile + 0.5) * TILE; }

  // Room coordinates of a world position.
  static roomX(x: f32): i32 { return TileMap.tileOf(x) / ROOM_W; }
  static roomY(y: f32): i32 { return TileMap.tileOf(y) / ROOM_H; }
}
