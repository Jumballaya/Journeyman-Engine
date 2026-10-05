// Area geometry: tiles and rooms. World units are pixels; the map's tile (0, 0)
// sits at the origin.
import { ROOM_W, ROOM_H } from "./areas";

export const TILE: f32 = 16;
// Pixels of misalignment forgiven when walking into an opening.
export const CORNER_SLIDE: f32 = 6;

export function tileOf(world: f32): i32 { return <i32>Mathf.floor(world / TILE); }
export function center(tile: i32): f32 { return (<f32>tile + 0.5) * TILE; }

// Room coordinates of a world position.
export function roomX(x: f32): i32 { return tileOf(x) / ROOM_W; }
export function roomY(y: f32): i32 { return tileOf(y) / ROOM_H; }
