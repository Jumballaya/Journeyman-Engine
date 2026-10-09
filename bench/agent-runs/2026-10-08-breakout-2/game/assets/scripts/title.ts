// Title screen: Enter starts a new game.
import { GameState, Input, Scene } from "@jm/runtime";
import { START_LIVES } from "./lib/rules";

export function onUpdate(dt: f32): void {
  if (Input.pressed("start")) {
    GameState.setNumber("score", 0);
    GameState.setNumber("lives", START_LIVES);
    Scene.load("game");
  }
}
