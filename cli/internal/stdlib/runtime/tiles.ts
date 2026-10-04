import { Overrides, spawn } from "./world";

// Fill a repeating vertical layer with a grid of tile centers. Overrides are
// reused without mutation; they specify texture, tint and movement. Tiles must
// have Transform, Velocity and ScrollWrap in their prefab.
export function tileGrid(prefab: string, columns: i32, rows: i32, x: f32, y: f32, width: f32, height: f32, overrides: Overrides): void {
  assert(width > 0 && height > 0, "tileGrid: positive tile size required");
  for (let row = 0; row < rows; row++) {
    for (let col = 0; col < columns; col++) spawn(prefab, x + <f32>col * width, y + <f32>row * height, overrides);
  }
}
