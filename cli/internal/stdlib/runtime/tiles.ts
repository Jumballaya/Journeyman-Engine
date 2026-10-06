import {
  __jmTileMapInfo, __jmTileMapAt, __jmTileMapSet, __jmTileMapIs, __jmTileMapPositionsOf, __jmTileMapObjects,
  __jmTileMapProperties, __jmTileMapShowLayer, __jmTileMapLoad, __jmTileMapMove,
} from "./env";
import { Entity } from "./entity";
import { Json, JsonValue } from "./json";
import { utf8, buf, cap, grow, text } from "./util";
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

const info = new StaticArray<f32>(6);  // width, height, tile width, tile height, origin x, origin y
const moved = new StaticArray<f32>(6);

// A shape placed on one of a map's object layers (in Tiled): a spawn point,
// a door, a trigger zone. World units; x/y is its bottom-left corner.
export class MapObject {
  id: i32 = 0;
  name: string = "";
  type: string = "";
  layer: string = "";
  x: f32 = 0;
  y: f32 = 0;
  width: f32 = 0;
  height: f32 = 0;
  point: bool = false;
  // Its custom properties, by name: properties.get("target").text().
  properties: JsonValue = JsonValue.object();

  get centerX(): f32 { return this.x + this.width * 0.5; }
  get centerY(): f32 { return this.y + this.height * 0.5; }
  // Whether a world point is inside it.
  contains(px: f32, py: f32): bool {
    return px >= this.x && px < this.x + this.width && py >= this.y && py < this.y + this.height;
  }
}

// A TileMapComponent's Tiled map (format: docs/content.md): tile layers drawn
// by the engine, with what is solid, tags ("deadly"), collision and objects.
// Tiles are known by their type (set in the tileset). Tile (0, 0) is the
// bottom-left; the map's entity sits at its bottom-left corner.
export class TileMap {
  constructor(readonly entity: Entity) {}

  // The map entity with this tag (e.g. "map").
  static find(tag: string): TileMap { return new TileMap(World.find(tag)); }

  get width(): i32 { this.read(); return <i32>info[0]; }
  get height(): i32 { this.read(); return <i32>info[1]; }
  get tileWidth(): f32 { this.read(); return info[2]; }
  get tileHeight(): f32 { this.read(); return info[3]; }

  // The type of the topmost tile at a tile ("" if none; the map's "outside" beyond its edges).
  at(tx: i32, ty: i32): string {
    let n = __jmTileMapAt(this.entity.index, this.entity.generation, tx, ty, buf(), cap());
    if (grow(n)) n = __jmTileMapAt(this.entity.index, this.entity.generation, tx, ty, buf(), cap());
    return text(n, "");
  }
  // Puts a tile of `type` there ("" clears it), on `layer` or by default the
  // topmost layer with a tile there. False if no tileset has the type.
  set(tx: i32, ty: i32, type: string, layer: string = ""): bool {
    const t = utf8(type), l = utf8(layer);
    return __jmTileMapSet(this.entity.index, this.entity.generation, tx, ty, t.dataStart, t.length, l.dataStart, l.length);
  }
  solid(tx: i32, ty: i32): bool { return this.is(tx, ty, "solid"); }
  // Whether a tile there has the tag (a true bool property, "solid" for solidity) on any layer.
  is(tx: i32, ty: i32, tag: string): bool {
    const t = utf8(tag);
    return __jmTileMapIs(this.entity.index, this.entity.generation, tx, ty, t.dataStart, t.length);
  }
  solidAt(x: f32, y: f32): bool { return this.solid(this.tileX(x), this.tileY(y)); }

  // Every tile of `type` on any layer, as [tx, ty, tx, ty, ...].
  positionsOf(type: string): i32[] {
    const t = utf8(type);
    let n = __jmTileMapPositionsOf(this.entity.index, this.entity.generation, t.dataStart, t.length, buf(), cap());
    if (grow(n)) n = __jmTileMapPositionsOf(this.entity.index, this.entity.generation, t.dataStart, t.length, buf(), cap());
    const list = Json.parse(text(n, "[]"));
    const out = new Array<i32>();
    for (let i = 0; i < list.length; i++) out.push(list.at(i).int());
    return out;
  }

  // The map's objects, optionally only those of a type (Tiled's "class").
  objects(type: string = ""): MapObject[] {
    let n = __jmTileMapObjects(this.entity.index, this.entity.generation, buf(), cap());
    if (grow(n)) n = __jmTileMapObjects(this.entity.index, this.entity.generation, buf(), cap());
    const list = Json.parse(text(n, "[]"));
    const out = new Array<MapObject>();
    for (let i = 0; i < list.length; i++) {
      const j = list.at(i);
      if (type.length > 0 && j.get("type").text() != type) continue;
      const o = new MapObject();
      o.id = j.get("id").int();
      o.name = j.get("name").text();
      o.type = j.get("type").text();
      o.layer = j.get("layer").text();
      o.x = <f32>j.get("x").number();
      o.y = <f32>j.get("y").number();
      o.width = <f32>j.get("width").number();
      o.height = <f32>j.get("height").number();
      o.point = j.get("point").bool();
      o.properties = j.get("properties");
      out.push(o);
    }
    return out;
  }
  // The object with this name, or null ("" finds none).
  object(name: string): MapObject | null {
    if (name.length == 0) return null;
    const all = this.objects();
    for (let i = 0; i < all.length; i++) if (all[i].name == name) return all[i];
    return null;
  }
  // The map's custom properties (Map > Map Properties in Tiled).
  get properties(): JsonValue {
    let n = __jmTileMapProperties(this.entity.index, this.entity.generation, buf(), cap());
    if (grow(n)) n = __jmTileMapProperties(this.entity.index, this.entity.generation, buf(), cap());
    return Json.parse(text(n, "{}"));
  }

  // Shows or hides every layer with this name (e.g. roofs as the player walks
  // in); false if there's none.
  showLayer(name: string, visible: bool): bool {
    const l = utf8(name);
    return __jmTileMapShowLayer(this.entity.index, this.entity.generation, l.dataStart, l.length, visible);
  }

  // Replaces the map with a .tmj file, e.g. "assets/maps/town.tmj"; false
  // (logged) if it can't be read.
  load(path: string): bool {
    const p = utf8(path);
    return __jmTileMapLoad(this.entity.index, this.entity.generation, p.dataStart, p.length);
  }

  // World position <-> tile.
  tileX(x: f32): i32 { this.read(); return <i32>Mathf.floor((x - info[4]) / info[2]); }
  tileY(y: f32): i32 { this.read(); return <i32>Mathf.floor((y - info[5]) / info[3]); }
  centerX(tx: i32): f32 { this.read(); return info[4] + (<f32>tx + 0.5) * info[2]; }
  centerY(ty: i32): f32 { this.read(); return info[5] + (<f32>ty + 0.5) * info[3]; }

  private read(): void { __jmTileMapInfo(this.entity.index, this.entity.generation, changetype<usize>(info), 24); }
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
