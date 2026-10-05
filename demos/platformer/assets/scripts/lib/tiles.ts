// Level geometry: tiles of 16 world units; the map's tile (0, 0) sits at the origin.
export const TILE: f32 = 16;

export function tileOf(world: f32): i32 { return <i32>Mathf.floor(world / TILE); }
export function center(tile: i32): f32 { return (<f32>tile + 0.5) * TILE; }

// Tag of a block's entity, so bumpers can find it.
export function tileTag(tx: i32, ty: i32): string {
  return "tile:" + tx.toString() + "," + ty.toString();
}
