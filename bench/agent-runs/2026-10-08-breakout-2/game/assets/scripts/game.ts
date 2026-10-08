// The game scene's controller: lays out the bricks, keeps the HUD current and
// deals a fresh wall when the last brick goes.
import { GameState, Overrides, Scene, UI, World, formatNumber, spawn } from "@jm/runtime";
import { BRICK_COLS, BRICK_ROWS, brickX, brickY } from "./lib/rules";

const ROW_COLORS: f32[] = [
  1.0, 0.3, 0.3,
  1.0, 0.6, 0.2,
  1.0, 0.9, 0.3,
  0.3, 0.9, 0.4,
  0.3, 0.6, 1.0,
];

for (let row = 0; row < BRICK_ROWS; row++) {
  for (let col = 0; col < BRICK_COLS; col++) {
    spawnBrick(row, col);
  }
}

function spawnBrick(row: i32, col: i32): void {
  spawn("brick", brickX(col), brickY(row), new Overrides()
    .tint(ROW_COLORS[row * 3], ROW_COLORS[row * 3 + 1], ROW_COLORS[row * 3 + 2]));
}

let started = false;

export function onUpdate(dt: f32): void {
  UI.setText("score", formatNumber(GameState.getNumber("score", 0), 5));
  UI.setText("lives", (<i32>GameState.getNumber("lives", 3)).toString());
  if (World.count("brick") > 0) started = true;
  else if (started) Scene.load("game");
}
