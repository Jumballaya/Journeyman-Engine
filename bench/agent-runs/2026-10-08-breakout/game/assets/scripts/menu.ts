// Title and game-over screens: Enter moves on to the scene in params.next.
import { GameState, Input, Key, Params, Scene, UI } from "@jm/runtime";
import { START_LIVES } from "./lib/rules";

const next = Params.text("next", "title");
if (UI.exists("final")) UI.setText("final", (<i32>GameState.getNumber("score", 0)).toString());

export function onUpdate(dt: f32): void {
  if (Input.keyPressed(Key.Enter)) {
    if (next == "main") {
      GameState.setNumber("score", 0);
      GameState.setNumber("lives", START_LIVES);
    }
    Scene.load(next);
  }
}
