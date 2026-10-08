// Game over: shows the final score; Enter goes back to the title.
import { GameState, Input, Scene, UI, formatNumber } from "@jm/runtime";

UI.setText("final", formatNumber(GameState.getNumber("score", 0), 5));

export function onUpdate(dt: f32): void {
  if (Input.pressed("start")) Scene.load("title");
}
