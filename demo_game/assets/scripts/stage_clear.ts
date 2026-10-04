// Results between stages: tallies kills, accuracy and bonuses, then continues.
import { Input, Music, UI } from "@jm/runtime";
import { blink, pad, percent, sfx } from "./lib/util";
import { Session, recordHiscore, stageName, stageScene } from "./lib/session";
import { addCrt, handleGlobalKeys } from "./lib/settings";
import { goTo } from "./lib/screens";

const stage = Session.clearedStage;
const accuracy = Session.shots > 0 ? Math.min(1, <f64>Session.hits / <f64>Session.shots) : 0;
const accuracyBonus = Math.round(accuracy * 100) * 100 * stage;
const noMissBonus: f64 = Session.deaths == 0 ? 20000 * stage : 0;
const music = new Music("music_title");
let t: f32 = 0;
let revealed: i32 = 0;  // result lines shown so far
let done = false;
let leaving = false;

UI.setText("title", "STAGE " + stage.toString() + " CLEAR!");
UI.setText("subtitle", stageName(stage));
UI.setText("score", pad(Session.score));
addCrt();
sfx("jingle_clear", 0.9);

function revealNext(): void {
  if (revealed == 0) UI.setText("kills", Session.kills.toString());
  else if (revealed == 1) UI.setText("accuracy", percent(accuracy));
  else if (revealed == 2) UI.setText("bonus", (<i64>accuracyBonus).toString());
  else UI.setText("nomiss", (<i64>noMissBonus).toString());
  revealed++;
  sfx("menu_move", 0.8);
}

function finish(): void {
  done = true;
  Session.addScore(accuracyBonus + noMissBonus);
  UI.setText("score", pad(Session.score));
  recordHiscore();
  sfx("powerup", 0.7);
  UI.removeClass("prompt", "invisible");
  music.play(0.6);
}

export function onUpdate(dt: f32): void {
  t += dt;
  handleGlobalKeys();

  if (!done) {
    // One line every 0.5s after a short pause; confirm reveals everything.
    const skip = Input.pressed("confirm");
    while (revealed < 4 && (skip || t > 1.0 + <f32>revealed * 0.5)) revealNext();
    if (revealed == 4 && (skip || t > 3.0)) finish();
    return;  // the press that skips doesn't also continue
  }

  UI.setStyle("prompt", "opacity", blink(t) ? "1" : "0.4");
  if (!leaving && Input.pressed("confirm")) {
    leaving = true;
    sfx("menu_select", 0.8);
    music.fadeOut(0.8);
    goTo(stageScene(stage + 1));
  }
}
