// Death screen: final score and record, then continue or return to the title.
import { screen } from "./lib/presentation";
import { Audio, Timer, UI } from "@jm/runtime";
import { scoreText } from "./lib/util";
import * as Session from "./lib/session";
import { hiscore, recordHiscore, stageName, stageScene } from "./lib/session";

const menu = screen.menu(["g-retry", "g-menu"]);
let leaving = false;
const inputDelay = new Timer(1);

const record = recordHiscore();
UI.setText("score", scoreText(Session.score.value));
UI.setText("hiscore", scoreText(hiscore()));
UI.setText("subtitle", "SHOT DOWN OVER " + stageName(Session.stage.value));
screen.setVisible("record", record);
menu.render();
screen.open();
Audio.play("jingle_gameover", 0.9);

export function onUpdate(dt: f32): void {
  inputDelay.tick(dt);
  screen.update();
  if (leaving || !inputDelay.ready) return;  // ignore buttons still held from gameplay

  const choice = menu.update();
  if (choice == "g-retry") {
    leaving = true;
    Session.continueGame();  // a continue costs the score, keeps the stage
    screen.goTo(stageScene(Session.stage.value));
  } else if (choice == "g-menu") {
    leaving = true;
    screen.goTo("title");
  }
}
