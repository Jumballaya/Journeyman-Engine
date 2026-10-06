// A box with velocity and gravity moving through the level's TileMap, and
// what it ran into. Position is the box's center, in world pixels.
import { TileBody, TileMap } from "@jm/runtime";
import { tileOf } from "./tiles";

export const GRAVITY: f32 = 900;
const MAX_FALL: f32 = 320;

export class Body extends TileBody {
  vx: f32 = 0;
  vy: f32 = 0;

  get hitWall(): bool { return this.hitX != 0; }
  // The tile bumped from below (nearest the body's center), or -1.
  get hitHeadTile(): i32 { return this.hitY > 0 ? this.hitTileX : -1; }
  get hitHeadRow(): i32 { return this.hitY > 0 ? this.hitTileY : -1; }

  // Applies gravity (scaled by `gravityScale`) and moves by velocity.
  fall(map: TileMap, dt: f32, gravityScale: f32 = 1): void {
    this.vy = Mathf.max(this.vy - GRAVITY * gravityScale * dt, -MAX_FALL);
    this.move(map, this.vx * dt, this.vy * dt);
    if (this.hitX != 0) this.vx = 0;
    if (this.hitY != 0) this.vy = 0;
  }

  // Whether the box touches a deadly tile (lava).
  touchesDeadly(map: TileMap): bool {
    const x0 = tileOf(this.x - this.halfW + 2), x1 = tileOf(this.x + this.halfW - 2);
    const row = tileOf(this.bottom + 2);
    for (let tx = x0; tx <= x1; tx++) {
      if (map.is(tx, row, "deadly")) return true;
    }
    return false;
  }

  // Whether there is ground just ahead in the direction of travel.
  groundAhead(map: TileMap, direction: f32): bool {
    return map.solidAt(this.x + direction * (this.halfW + 1), this.bottom - 1);
  }
}
