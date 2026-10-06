import {
  __jmTileMapInfo, __jmTileMapAt, __jmTileMapSet, __jmTileMapIs, __jmTileMapLoad, __jmTileMapSetRows,
  __jmTileMapMove,
} from "./env";
import { Entity } from "./entity";
import { JsonValue } from "./json";
import { utf8 } from "./util";
import { Overrides, World, spawn } from "./world";

// Grid dimensions in world units. x/y locate the first tile's center.
export class TileGrid {
  columns: i32 = 1;
  rows: i32 = 1;
  width: f32 = 32;
  height: f32 = 32;
  x: f32 = 0;
  y: f32 = 0;
}

// Lay out tiles without mutating shared overrides. The prefab owns its
// components; overrides choose texture, tint, scale, velocity and wrapping.
export function tileGrid(prefab: string, grid: TileGrid, overrides: Overrides): void {
  assert(grid.width > 0 && grid.height > 0, "tileGrid: positive tile size required");
  for (let row = 0; row < grid.rows; row++) {
    for (let col = 0; col < grid.columns; col++) {
      spawn(prefab, grid.x + <f32>col * grid.width, grid.y + <f32>row * grid.height, overrides);
    }
  }
}

const info = new StaticArray<f32>(5);  // width, height, tile size, origin x, origin y
const moved = new StaticArray<f32>(6);

// A TileMapComponent's grid (format: docs/content.md): ASCII rows of tiles
// drawn by the engine, with what is solid, tags ("deadly") and collision.
// Tile (0, 0) is the bottom-left; the map's entity sits at its bottom-left corner.
export class TileMap {
  constructor(readonly entity: Entity) {}

  // The map entity with this tag (e.g. "map").
  static find(tag: string): TileMap { return new TileMap(World.find(tag)); }

  get width(): i32 { this.read(); return <i32>info[0]; }
  get height(): i32 { this.read(); return <i32>info[1]; }
  get tileSize(): f32 { this.read(); return info[2]; }

  // The character at a tile (the map's "outside" beyond its edges).
  at(tx: i32, ty: i32): string {
    const c = __jmTileMapAt(this.entity.index, this.entity.generation, tx, ty);
    return c < 0 ? "" : String.fromCharCode(c);
  }
  // Changes a tile (e.g. a broken brick to "."); ignored outside the map.
  set(tx: i32, ty: i32, c: string): void {
    __jmTileMapSet(this.entity.index, this.entity.generation, tx, ty, c.charCodeAt(0));
  }
  solid(tx: i32, ty: i32): bool { return this.is(tx, ty, "solid"); }
  // Whether the tile's definition has the tag ("solid" for solidity).
  is(tx: i32, ty: i32, tag: string): bool {
    const t = utf8(tag);
    return __jmTileMapIs(this.entity.index, this.entity.generation, tx, ty, t.dataStart, t.length);
  }
  solidAt(x: f32, y: f32): bool { return this.solid(this.tileX(x), this.tileY(y)); }

  // Replaces the grid with a text file's lines (top row first), e.g.
  // "assets/maps/town.txt"; false (logged) if it can't be read.
  load(path: string): bool {
    const p = utf8(path);
    return __jmTileMapLoad(this.entity.index, this.entity.generation, p.dataStart, p.length);
  }

  // Replaces the whole grid (rows top first).
  setRows(rows: string[]): void {
    const r = utf8(JsonValue.strings(rows).toString());
    __jmTileMapSetRows(this.entity.index, this.entity.generation, r.dataStart, r.length);
  }

  // World position <-> tile.
  tileX(x: f32): i32 { this.read(); return <i32>Mathf.floor((x - info[3]) / info[2]); }
  tileY(y: f32): i32 { this.read(); return <i32>Mathf.floor((y - info[4]) / info[2]); }
  centerX(tx: i32): f32 { this.read(); return info[3] + (<f32>tx + 0.5) * info[2]; }
  centerY(ty: i32): f32 { this.read(); return info[4] + (<f32>ty + 0.5) * info[2]; }

  // Every tile holding one of `chars`, as [tx, ty, tx, ty, ...]: where to
  // spawn the things a map marks.
  positionsOf(chars: string): i32[] {
    const out = new Array<i32>();
    const w = this.width, h = this.height;
    for (let ty = 0; ty < h; ty++) {
      for (let tx = 0; tx < w; tx++) {
        if (chars.includes(this.at(tx, ty))) {
          out.push(tx);
          out.push(ty);
        }
      }
    }
    return out;
  }

  private read(): void { __jmTileMapInfo(this.entity.index, this.entity.generation, changetype<usize>(info), 20); }
}

// A box moving through a TileMap without entering solid tiles, one axis at a
// time, stopping flush against them. Position is the center, in world units.
export class TileBody {
  x: f32 = 0;
  y: f32 = 0;
  // What the last move ran into: -1/+1 for the side blocked on each axis, and
  // the solid tile met last (nearest the body's center), or -1.
  hitX: i32 = 0;
  hitY: i32 = 0;
  hitTileX: i32 = -1;
  hitTileY: i32 = -1;

  constructor(public halfW: f32, public halfH: f32) {}

  get onGround(): bool { return this.hitY < 0; }
  get blocked(): bool { return this.hitX != 0 || this.hitY != 0; }
  get bottom(): f32 { return this.y - this.halfH; }
  get top(): f32 { return this.y + this.halfH; }

  // Moves by (dx, dy). With `slide` > 0, a move blocked along one axis nudges
  // up to `slide` units sideways toward an opening, so doorways are easy to enter.
  move(map: TileMap, dx: f32, dy: f32, slide: f32 = 0): void {
    __jmTileMapMove(map.entity.index, map.entity.generation, this.x, this.y, this.halfW, this.halfH,
                    dx, dy, slide, changetype<usize>(moved), 24);
    this.x = moved[0];
    this.y = moved[1];
    this.hitX = <i32>moved[2];
    this.hitY = <i32>moved[3];
    this.hitTileX = <i32>moved[4];
    this.hitTileY = <i32>moved[5];
  }
}
