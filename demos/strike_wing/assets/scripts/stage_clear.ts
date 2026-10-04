// Results between stages: tallies kills, accuracy and bonuses, then continues.
import { screen } from "./lib/presentation";
import { Audio, Timeline, blink, formatPercent, Input, Music, UI } from "@jm/runtime";
import { scoreText } from "./lib/util";
import * as Session from "./lib/session";
import { recordHiscore, stageName, stageScene } from "./lib/session";

const stage = Session.clearedStage.value;
const accuracy = Session.shots.value > 0 ? Math.min(1, <f64>Session.hits.value / <f64>Session.shots.value) : 0;
const accuracyBonus = Math.round(accuracy * 100) * 100 * stage;
const noMissBonus: f64 = Session.deaths.value == 0 ? 20000 * stage : 0;
const music = new Music("music_title");
let t: f32 = 0;
enum Result { Kills, Accuracy, Bonus, NoMiss, Total }
const reveals = new Timeline<Result>()
  .at(1, Result.Kills).at(1.5, Result.Accuracy).at(2, Result.Bonus)
  .at(2.5, Result.NoMiss).at(3, Result.Total);
let leaving = false;

UI.setText("title", "STAGE " + stage.toString() + " CLEAR!");
UI.setText("subtitle", stageName(stage));
UI.setText("score", scoreText(Session.score.value));
screen.open();
Audio.play("jingle_clear", 0.9);

function reveal(result: Result): void {
  if (result == Result.Total) { finish(); return; }
  if (result == Result.Kills) UI.setText("kills", Session.kills.value.toString());
  else if (result == Result.Accuracy) UI.setText("accuracy", formatPercent(accuracy));
  else if (result == Result.Bonus) UI.setText("bonus", (<i64>accuracyBonus).toString());
  else UI.setText("nomiss", (<i64>noMissBonus).toString());
  Audio.play("menu_move", 0.8);
}

function finish(): void {
  Session.score.add(accuracyBonus + noMissBonus);
  UI.setText("score", scoreText(Session.score.value));
  recordHiscore();
  Audio.play("powerup", 0.7);
  UI.removeClass("prompt", "invisible");
  music.play(0.6);
}

export function onUpdate(dt: f32): void {
  t += dt;
  screen.update();

  if (!reveals.done) {
    // Confirm drains the sequence, but never also continues to the next stage.
    if (Input.pressed("confirm")) reveals.finish();
    reveals.update(dt, reveal);
    return;
  }

  UI.opacity("prompt", blink(t, 2, 1, 0.4));
  if (!leaving && Input.pressed("confirm")) {
    leaving = true;
    Audio.play("menu_select", 0.8);
    music.fadeOut(0.8);
    screen.goTo(stageScene(stage + 1));
  }
}
