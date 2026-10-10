// A between-levels screen (intro, game over, victory). Fills whichever of
// #world, #lives, #score, #record its document has, then moves on to param
// "next" ("level": the current level's scene): after "seconds", or on confirm when seconds is 0.
import { Input, Params, Scene, Sound, UI, formatNumber } from "@jm/runtime";
import { levelScene } from "./lib/levels";
import { Session } from "./lib/session";

const next = Params.text("next", "title") == "level" ? levelScene(Session.level) : Params.text("next", "title");
const seconds = <f32>Params.number("seconds");
let t: f32 = 0;
let leaving = false;

UI.setText("world", Session.level);
UI.setText("lives", Session.lives.toString());
UI.setText("score", formatNumber(Session.score, 6));
if (Params.number("record") > 0) UI.setVisible("record", Session.recordHiscore(), "hidden");
const jingle = Params.text("jingle");
if (jingle.length > 0) new Sound(jingle).play(0.7);

export function onUpdate(dt: f32): void {
  t += dt;
  if (leaving) return;
  const timeUp = seconds > 0 && t >= seconds;
  const confirmed = seconds == 0 && t > 1 && Input.justPressed("confirm");
  if (!timeUp && !confirmed) return;
  leaving = true;
  Scene.transition(next, 0.4);
}
