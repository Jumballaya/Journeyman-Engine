// The map as a grid of tiles: who stands where, what blocks, what sees what.
// Tile (0, 0) is the bottom-left; everyone stands on a tile's center, their
// sprite lifted so the feet rest on it.
import { Entity, TileMap, World } from "@jm/runtime";

export const LIFT: f32 = 4;  // people are 24px tall on 16px tiles

export class Cell {
  constructor(public x: i32, public y: i32) {}
  equals(o: Cell): bool { return this.x == o.x && this.y == o.y; }
}

export class Grid {
  map: TileMap;

  constructor() { this.map = TileMap.find("Map"); }

  get width(): i32 { return this.map.width; }
  get height(): i32 { return this.map.height; }
  inside(x: i32, y: i32): bool { return x >= 0 && y >= 0 && x < this.width && y < this.height; }

  worldX(x: i32): f32 { return this.map.centerX(x); }
  worldY(y: i32): f32 { return this.map.centerY(y); }
  cellOf(e: Entity): Cell {
    const t = e.transform;
    return new Cell(this.map.tileX(t.x), this.map.tileY(t.y - LIFT));
  }

  // Puts someone on a tile (sprites are drawn in front of those above them).
  place(e: Entity, x: f32, y: f32): void {
    e.transform.setPosition(x, y + LIFT);
    e.transform.z = 10 + (<f32>this.height - y / this.map.tileSize) * 0.01;
  }
  placeAt(e: Entity, c: Cell): void { this.place(e, this.worldX(c.x), this.worldY(c.y)); }

  solid(x: i32, y: i32): bool { return !this.inside(x, y) || this.map.solid(x, y); }

  // Someone (or something) stands there: people, enemies, caches.
  occupant(x: i32, y: i32): Entity {
    const tags = ["player", "npc", "enemy", "cache"];
    for (let t = 0; t < tags.length; t++) {
      const all = World.findAll(tags[t]);
      for (let i = 0; i < all.length; i++) {
        const c = this.cellOf(all[i]);
        if (c.x == x && c.y == y) return all[i];
      }
    }
    return Entity.NONE;
  }

  walkable(x: i32, y: i32): bool { return !this.solid(x, y) && this.occupant(x, y).isNone; }

  // Walls, roofs and rocks block sight; people don't.
  sees(a: Cell, b: Cell): bool {
    let x0 = a.x, y0 = a.y;
    const dx = abs(b.x - x0), dy = -abs(b.y - y0);
    const sx = x0 < b.x ? 1 : -1, sy = y0 < b.y ? 1 : -1;
    let err = dx + dy;
    while (true) {
      if (x0 == b.x && y0 == b.y) return true;
      if (!(x0 == a.x && y0 == a.y) && this.solid(x0, y0)) return false;
      const e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }

  // Steps along the shortest walkable path from `from` toward a tile from which
  // `to` is in reach (`reach` tiles, Manhattan, and in sight); empty if none.
  // Hazards (slag) are walked around when there's any other way.
  pathToward(from: Cell, to: Cell, reach: i32): Cell[] {
    const safe = this.search(from, to, reach, true);
    return safe.length > 0 || this.inReach(from, to, reach) ? safe : this.search(from, to, reach, false);
  }

  hazard(c: Cell): bool { return this.map.is(c.x, c.y, "hazard"); }

  private inReach(c: Cell, to: Cell, reach: i32): bool { return reaches(c, to, reach) && this.sees(c, to); }

  private search(from: Cell, to: Cell, reach: i32, avoidHazards: bool): Cell[] {
    const w = this.width, h = this.height;
    const prev = new Array<i32>(w * h).fill(-2);
    const queue: i32[] = [from.y * w + from.x];
    prev[from.y * w + from.x] = -1;
    let head = 0;
    let found = -1;
    while (head < queue.length) {
      const cur = queue[head++];
      const cx = cur % w, cy = cur / w;
      if (this.inReach(new Cell(cx, cy), to, reach)) { found = cur; break; }
      const nx = [cx + 1, cx - 1, cx, cx], ny = [cy, cy, cy + 1, cy - 1];
      for (let k = 0; k < 4; k++) {
        const x = nx[k], y = ny[k];
        if (!this.inside(x, y) || prev[y * w + x] != -2 || !this.walkable(x, y)) continue;
        if (avoidHazards && this.map.is(x, y, "hazard") && !(x == to.x && y == to.y)) continue;
        prev[y * w + x] = cur;
        queue.push(y * w + x);
      }
    }
    const path: Cell[] = [];
    for (let at = found; at >= 0 && prev[at] != -1; at = prev[at]) path.unshift(new Cell(at % w, at / w));
    return path;
  }
}

export function dist(a: Cell, b: Cell): i32 { return abs(a.x - b.x) + abs(a.y - b.y); }

// Whether `b` is within `range` of `a`: tiles walked (Manhattan), except that
// arm's length (range 1) reaches the diagonals too.
export function reaches(a: Cell, b: Cell, range: i32): bool {
  if (range == 1) return max(abs(a.x - b.x), abs(a.y - b.y)) <= 1;
  return dist(a, b) <= range;
}
