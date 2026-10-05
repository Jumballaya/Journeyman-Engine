// A box moving top-down through a TileMap without entering solid tiles, one
// axis at a time. Position is the box's center, in world pixels.
import { TileMap } from "./tiles";

const CORNER_SLIDE: f32 = 6;  // pixels of misalignment forgiven at openings

export class Body {
  x: f32 = 0;
  y: f32 = 0;
  // The first solid tile the last move() ran into, or -1.
  blockedX: i32 = -1;
  blockedY: i32 = -1;

  constructor(public halfW: f32, public halfH: f32) {}

  get blocked(): bool { return this.blockedX >= 0; }

  // Moves by (dx, dy), stopping at solid tiles. A blocked move that is a few
  // pixels off an opening slides toward it, so doorways are easy to enter.
  move(map: TileMap, dx: f32, dy: f32): void {
    this.blockedX = -1;
    this.blockedY = -1;
    this.x += dx;
    if (this.hit(map)) {
      this.x -= dx;
      if (dy == 0) this.y += this.slide(map, dx, 0, Mathf.abs(dx));
    }
    this.y += dy;
    if (this.hit(map)) {
      this.y -= dy;
      if (dx == 0) this.x += this.slide(map, 0, dy, Mathf.abs(dy));
    }
  }

  // The sideways step (at most `step`) toward the nearest opening within
  // CORNER_SLIDE pixels that would let (dx, dy) through; 0 if none.
  private slide(map: TileMap, dx: f32, dy: f32, step: f32): f32 {
    const x = this.x, y = this.y;
    for (let off: f32 = 1; off <= CORNER_SLIDE; off++) {
      for (let sign: f32 = -1; sign <= 1; sign += 2) {
        this.x = x + dx + (dx == 0 ? sign * off : 0);
        this.y = y + dy + (dy == 0 ? sign * off : 0);
        const open = !this.hit(map);
        this.x = x;
        this.y = y;
        if (open) return sign * Mathf.min(off, step);
      }
    }
    return 0;
  }

  // Whether the box overlaps a solid tile; records the first one.
  private hit(map: TileMap): bool {
    const x0 = TileMap.tileOf(this.x - this.halfW), x1 = TileMap.tileOf(this.x + this.halfW);
    const y0 = TileMap.tileOf(this.y - this.halfH), y1 = TileMap.tileOf(this.y + this.halfH);
    for (let ty = y0; ty <= y1; ty++) {
      for (let tx = x0; tx <= x1; tx++) {
        if (!map.solid(tx, ty)) continue;
        if (this.blockedX < 0) {
          this.blockedX = tx;
          this.blockedY = ty;
        }
        return true;
      }
    }
    return false;
  }
}
