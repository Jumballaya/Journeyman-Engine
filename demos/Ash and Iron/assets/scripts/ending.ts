// The end of the slice: a few words, the tally, and back to the title.
import { Key, GameState, Input, Music, Scene, UI } from "@jm/runtime";
import * as hero from "./lib/state";
import * as slots from "./lib/slots";

const music = new Music("music_victory");
let leaving = false;
let wait: f32 = 1.5;

music.play(0.5);
UI.setText("end-level", "Level " + hero.level().toString());
UI.setText("end-time", slots.clock(GameState.getNumber("time.played")));
UI.setText("end-scrip", hero.scrip().toString() + " scrip");

export function onUpdate(dt: f32): void {
  if (leaving) return;
  wait -= dt;
  if (wait > 0 || !(Input.pressed("confirm") || Input.keyPressed(Key.MouseLeft))) return;
  leaving = true;
  music.fadeOut(1);
  Scene.transition("title", 1);
}
