// Win screen after the boss: final score, record and credits.
import { screen } from "./lib/presentation";
import { Audio, Timer, blink, Input, Music, UI } from "@jm/runtime";
import { scoreText } from "./lib/util";
import * as Session from "./lib/session";
import { hiscore, recordHiscore } from "./lib/session";

const musicDelay = new Timer(7.5); // after the victory jingle
const music = new Music("music_title");
let leaving = false;
let t: f32 = 0;

const record = recordHiscore();
UI.setText("score", scoreText(Session.score.value));
UI.setText("hiscore", scoreText(hiscore()));
screen.setVisible("record", record);
screen.open();
Audio.play("jingle_victory");

export function onUpdate(dt: f32): void {
  t += dt;
  screen.update();
  if (musicDelay.tick(dt)) music.play(0.8);
  if (t < 2.0) return;

  UI.removeClass("prompt", "invisible");
  UI.opacity("prompt", blink(t, 2, 1, 0.4));
  if (!leaving && Input.pressed("confirm")) {
    leaving = true;
    Audio.play("menu_select", 0.8);
    music.fadeOut(0.8);
    screen.goTo("title");
  }
}
