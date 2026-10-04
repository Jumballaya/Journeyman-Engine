// Death screen: final score and record, then continue or return to the title.
import { UI } from "@jm/runtime";
import { pad, sfx } from "./lib/util";
import { Session, hiscore, recordHiscore, stageName, stageScene } from "./lib/session";
import { addCrt, handleGlobalKeys } from "./lib/settings";
import { Menu, goTo, setVisible } from "./lib/screens";

const menu = new Menu(["g-retry", "g-menu"]);
let leaving = false;
let t: f32 = 0;

const record = recordHiscore();
UI.setText("score", pad(Session.score));
UI.setText("hiscore", pad(hiscore()));
UI.setText("subtitle", "SHOT DOWN OVER " + stageName(Session.stage));
setVisible("record", record);
menu.render();
addCrt();
sfx("jingle_gameover", 0.9);

export function onUpdate(dt: f32): void {
  t += dt;
  handleGlobalKeys();
  if (leaving || t < 1.0) return;  // ignore buttons still held from gameplay

  const choice = menu.update();
  if (choice == "g-retry") {
    leaving = true;
    Session.continueGame();  // a continue costs the score, keeps the stage
    goTo(stageScene(Session.stage));
  } else if (choice == "g-menu") {
    leaving = true;
    goTo("title");
  }
}
