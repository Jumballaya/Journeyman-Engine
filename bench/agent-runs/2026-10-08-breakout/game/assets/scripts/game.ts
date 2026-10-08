// Lays out the bricks; refills the wall when it is cleared.
import { Overrides, World, spawn } from "@jm/runtime";
import { BRICK_COLS, BRICK_ROWS, brickX, brickY } from "./lib/rules";

const ROW_R: f32[] = [1.0, 1.0, 0.9, 0.3, 0.3];
const ROW_G: f32[] = [0.3, 0.6, 0.9, 0.9, 0.6];
const ROW_B: f32[] = [0.3, 0.2, 0.3, 0.4, 1.0];

function buildWall(): void {
  for (let r = 0; r < BRICK_ROWS; r++) {
    for (let c = 0; c < BRICK_COLS; c++) {
      spawn("brick", brickX(c), brickY(r), new Overrides().tint(ROW_R[r], ROW_G[r], ROW_B[r]));
    }
  }
}

buildWall();
let waiting: i32 = 2;

export function onUpdate(dt: f32): void {
  if (waiting > 0) { waiting--; return; }
  if (World.count("brick") == 0) { buildWall(); waiting = 2; }
}
