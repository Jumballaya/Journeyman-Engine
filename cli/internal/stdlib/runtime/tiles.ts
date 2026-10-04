import { Overrides, spawn } from "./world";

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
