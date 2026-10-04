// Between-stage results: tallies kills, accuracy and bonuses, then continues.
import { GameState, Input, UI, Sound, Bus } from "@jm/runtime";
import {
  pad, sfx, setVisible, addCrt, handleGlobalKeys, commitHiscore, addScore, stageName, stageScene,
  transition, SHADER_WIPE,
} from "./lib/game";

let started = false;
let t: f32 = 0;
let stage: i32 = 1;
let kills: f64 = 0;
let accuracy: f64 = 0;
let bonus: f64 = 0;
let noMiss: f64 = 0;
let step: i32 = 0;      // which line is being revealed
let done = false;
let leaving = false;
const music = new Sound("assets/sounds/music_title.wav", Bus.Music);

function start(): void {
  started = true;
  stage = <i32>GameState.getNumber("clearedStage", 1);
  kills = GameState.getNumber("stageKills");
  const shots = GameState.getNumber("stageShots");
  accuracy = shots > 0 ? Math.min(1, GameState.getNumber("stageHits") / shots) : 0;
  bonus = Math.round(accuracy * 100) * 100 * stage;
  noMiss = GameState.getNumber("stageDeaths") == 0 ? 20000 * stage : 0;
  UI.setText("title", "STAGE " + stage.toString() + " CLEAR!");
  UI.setText("subtitle", stageName(stage));
  UI.setText("score", pad(GameState.getNumber("score")));
  addCrt();
  sfx("jingle_clear", 0.9);
}

function reveal(id: string, value: string): void {
  UI.setText(id, value);
  sfx("menu_move", 0.8);
}

export function onUpdate(dt: f32): void {
  if (!started) start();
  t += dt;
  handleGlobalKeys();

  // Reveal one line every 0.5s after a short pause (confirm skips ahead).
  const skip = Input.pressed("confirm") && !done;
  while ((t > 1.0 + <f32>step * 0.5 || skip) && step < 5) {
    if (step == 0) reveal("kills", (<i32>kills).toString());
    else if (step == 1) reveal("accuracy", (<i32>Math.round(accuracy * 100)).toString() + "%");
    else if (step == 2) reveal("bonus", (<i64>bonus).toString());
    else if (step == 3) reveal("nomiss", (<i64>noMiss).toString());
    else {
      addScore(bonus + noMiss);
      UI.setText("score", pad(GameState.getNumber("score")));
      commitHiscore();
      sfx("powerup", 0.7);
      done = true;
      UI.removeClass("prompt", "invisible");
      music.play(0.6, true);
      if (skip) return;  // don't let the same press also continue
    }
    step++;
  }

  if (done) UI.setStyle("prompt", "opacity", Mathf.floor(t * 2) % 2 == 0 ? "1" : "0.4");
  if (done && !leaving && Input.pressed("confirm")) {
    leaving = true;
    sfx("menu_select", 0.8);
    music.fadeOut(0.8);
    transition(stageScene(stage + 1), SHADER_WIPE, 1.0);
  }
}
