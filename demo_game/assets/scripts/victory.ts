// Win screen after the boss: final score, record, credits.
import { GameState, Input, UI, Sound, Bus } from "@jm/runtime";
import {
  pad, sfx, setVisible, addCrt, handleGlobalKeys, commitHiscore, savedHiscore, transition,
  SCENE_TITLE, SHADER_WIPE,
} from "./lib/game";

const music = new Sound("assets/sounds/music_title.wav", Bus.Music);
let started = false;
let leaving = false;
let musicStarted = false;
let t: f32 = 0;

function start(): void {
  started = true;
  const record = commitHiscore();
  UI.setText("score", pad(GameState.getNumber("score")));
  UI.setText("hiscore", pad(savedHiscore()));
  setVisible("record", record);
  addCrt();
  sfx("jingle_victory", 1.0);
}

export function onUpdate(dt: f32): void {
  if (!started) start();
  t += dt;
  handleGlobalKeys();
  if (!musicStarted && t > 7.5) {
    musicStarted = true;
    music.play(0.8, true);
  }
  if (t > 2.0) {
    setVisible("prompt", true);
    UI.setStyle("prompt", "opacity", Mathf.floor(t * 2) % 2 == 0 ? "1" : "0.4");
    if (!leaving && Input.pressed("confirm")) {
      leaving = true;
      sfx("menu_select", 0.8);
      music.fadeOut(0.8);
      transition(SCENE_TITLE, SHADER_WIPE, 1.0);
    }
  }
}
