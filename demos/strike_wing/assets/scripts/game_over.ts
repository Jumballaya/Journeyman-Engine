// Death screen: final score and record, then continue or return to the title.
import { Timer, UI } from "@jm/runtime";
import { scoreText, sfx } from "./lib/util";
import { Session, hiscore, recordHiscore, stageName, stageScene } from "./lib/session";
import { addCrt, handleGlobalKeys } from "./lib/settings";
import { gameMenu, goTo, setVisible } from "./lib/screens";

const menu = gameMenu(["g-retry", "g-menu"]);
let leaving = false;
const inputDelay = new Timer(1);

const record = recordHiscore();
UI.setText("score", scoreText(Session.score));
UI.setText("hiscore", scoreText(hiscore()));
UI.setText("subtitle", "SHOT DOWN OVER " + stageName(Session.stage));
setVisible("record", record);
menu.render();
addCrt();
sfx("jingle_gameover", 0.9);

export function onUpdate(dt: f32): void {
  inputDelay.tick(dt);
  handleGlobalKeys();
  if (leaving || !inputDelay.ready) return;  // ignore buttons still held from gameplay

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
