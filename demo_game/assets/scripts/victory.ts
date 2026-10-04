// Win screen after the boss: final score, record and credits.
import { Input, Music, UI } from "@jm/runtime";
import { blink, pad, sfx } from "./lib/util";
import { Session, hiscore, recordHiscore } from "./lib/session";
import { addCrt, handleGlobalKeys } from "./lib/settings";
import { goTo, setVisible } from "./lib/screens";

const MUSIC_AT: f32 = 7.5;  // after the victory jingle
const music = new Music("music_title");
let leaving = false;
let t: f32 = 0;

const record = recordHiscore();
UI.setText("score", pad(Session.score));
UI.setText("hiscore", pad(hiscore()));
setVisible("record", record);
addCrt();
sfx("jingle_victory");

export function onUpdate(dt: f32): void {
  const before = t;
  t += dt;
  handleGlobalKeys();
  if (before < MUSIC_AT && t >= MUSIC_AT) music.play(0.8);
  if (t < 2.0) return;

  UI.removeClass("prompt", "invisible");
  UI.setStyle("prompt", "opacity", blink(t) ? "1" : "0.4");
  if (!leaving && Input.pressed("confirm")) {
    leaving = true;
    sfx("menu_select", 0.8);
    music.fadeOut(0.8);
    goTo("title");
  }
}
