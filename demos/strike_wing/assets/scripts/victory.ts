// Win screen after the boss: final score, record and credits.
import { Timer, blink, Input, Music, UI } from "@jm/runtime";
import { scoreText, sfx } from "./lib/util";
import { Session, hiscore, recordHiscore } from "./lib/session";
import { addCrt, handleGlobalKeys } from "./lib/settings";
import { goTo, setVisible } from "./lib/screens";

const musicDelay = new Timer(7.5); // after the victory jingle
const music = new Music("music_title");
let leaving = false;
let t: f32 = 0;

const record = recordHiscore();
UI.setText("score", scoreText(Session.score));
UI.setText("hiscore", scoreText(hiscore()));
setVisible("record", record);
addCrt();
sfx("jingle_victory");

export function onUpdate(dt: f32): void {
  t += dt;
  handleGlobalKeys();
  if (musicDelay.tick(dt)) music.play(0.8);
  if (t < 2.0) return;

  UI.removeClass("prompt", "invisible");
  UI.opacity("prompt", blink(t, 2, 1, 0.4));
  if (!leaving && Input.pressed("confirm")) {
    leaving = true;
    sfx("menu_select", 0.8);
    music.fadeOut(0.8);
    goTo("title");
  }
}
