// A box that moves through a TileMap without entering solid tiles, moving
// one axis at a time. Position is the box's center, in world pixels.
import { TileMap, TILE } from "./tiles";

export const GRAVITY: f32 = 900;
const MAX_FALL: f32 = 320;

export class Body {
  x: f32 = 0;
  y: f32 = 0;
  vx: f32 = 0;
  vy: f32 = 0;
  // What the last move() ran into.
  onGround: bool = false;
  hitWall: bool = false;
  hitHeadTile: i32 = -1;  // tile column bumped from below, or -1
  hitHeadRow: i32 = -1;

  constructor(public halfW: f32, public halfH: f32) {}

  get bottom(): f32 { return this.y - this.halfH; }
  get top(): f32 { return this.y + this.halfH; }

  // Applies gravity (scaled by `gravityScale`) and moves by velocity.
  move(map: TileMap, dt: f32, gravityScale: f32 = 1): void {
    this.vy = Mathf.max(this.vy - GRAVITY * gravityScale * dt, -MAX_FALL);
    this.hitWall = false;
    this.hitHeadTile = -1;
    this.hitHeadRow = -1;

    this.x += this.vx * dt;
    if (this.vx != 0 && this.blocked(map)) {
      const edge = this.vx > 0 ? this.x + this.halfW : this.x - this.halfW;
      const tile = TileMap.tileOf(edge);
      this.x = this.vx > 0 ? <f32>tile * TILE - this.halfW - 0.01 : <f32>(tile + 1) * TILE + this.halfW + 0.01;
      this.vx = 0;
      this.hitWall = true;
    }

    const wasRising = this.vy > 0;
    this.y += this.vy * dt;
    this.onGround = false;
    if (this.blocked(map)) {
      if (wasRising) {
        const row = TileMap.tileOf(this.top);
        this.y = <f32>row * TILE - this.halfH - 0.01;
        this.hitHeadRow = row;
        this.hitHeadTile = TileMap.tileOf(this.x);  // the block above the body's center
        if (!map.solid(this.hitHeadTile, row)) this.hitHeadTile = this.nearestSolid(map, row);
      } else {
        this.y = <f32>(TileMap.tileOf(this.bottom) + 1) * TILE + this.halfH + 0.01;
        this.onGround = true;
      }
      this.vy = 0;
    }
  }

  // Whether the box overlaps a solid tile.
  blocked(map: TileMap): bool {
    const x0 = TileMap.tileOf(this.x - this.halfW), x1 = TileMap.tileOf(this.x + this.halfW);
    const y0 = TileMap.tileOf(this.bottom), y1 = TileMap.tileOf(this.top);
    for (let ty = y0; ty <= y1; ty++) {
      for (let tx = x0; tx <= x1; tx++) {
        if (map.solid(tx, ty)) return true;
      }
    }
    return false;
  }

  // Whether the box touches a deadly tile (lava).
  touchesDeadly(map: TileMap): bool {
    const x0 = TileMap.tileOf(this.x - this.halfW + 2), x1 = TileMap.tileOf(this.x + this.halfW - 2);
    const row = TileMap.tileOf(this.bottom + 2);
    for (let tx = x0; tx <= x1; tx++) {
      if (map.deadly(tx, row)) return true;
    }
    return false;
  }

  // Whether there is ground just ahead in the direction of travel.
  groundAhead(map: TileMap, direction: f32): bool {
    const probe = this.x + direction * (this.halfW + 1);
    return map.solid(TileMap.tileOf(probe), TileMap.tileOf(this.bottom - 1));
  }

  private nearestSolid(map: TileMap, row: i32): i32 {
    const left = TileMap.tileOf(this.x - this.halfW), right = TileMap.tileOf(this.x + this.halfW);
    return map.solid(left, row) ? left : right;
  }
}
