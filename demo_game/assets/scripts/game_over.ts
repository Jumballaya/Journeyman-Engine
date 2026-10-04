// Death screen: final score, high score, continue or quit to the title.
import { GameState, UI } from "@jm/runtime";
import {
  Menu, pad, sfx, setVisible, addCrt, handleGlobalKeys, commitHiscore, savedHiscore, stageName,
  stageScene, transition, SCENE_TITLE, SHADER_WIPE,
} from "./lib/game";

const menu = new Menu(["g-retry", "g-menu"]);
let started = false;
let leaving = false;
let t: f32 = 0;

function start(): void {
  started = true;
  const record = commitHiscore();
  const stage = <i32>GameState.getNumber("stage", 1);
  UI.setText("score", pad(GameState.getNumber("score")));
  UI.setText("hiscore", pad(savedHiscore()));
  UI.setText("subtitle", "SHOT DOWN OVER " + stageName(stage));
  setVisible("record", record);
  menu.render();
  addCrt();
  sfx("jingle_gameover", 0.9);
}

export function onUpdate(dt: f32): void {
  if (!started) start();
  t += dt;
  handleGlobalKeys();
  if (leaving || t < 1.0) return;  // ignore inputs held from gameplay

  const choice = menu.update();
  if (choice == 0) {
    leaving = true;
    // A continue costs your score but keeps your progress.
    const stage = <i32>GameState.getNumber("stage", 1);
    GameState.setNumber("score", 0);
    GameState.setNumber("lives", 2);
    GameState.setNumber("bombs", 3);
    GameState.setNumber("power", 1);
    GameState.setNumber("snapshotStage", 0);
    transition(stageScene(stage), SHADER_WIPE, 1.0);
  } else if (choice == 1) {
    leaving = true;
    transition(SCENE_TITLE, SHADER_WIPE, 1.0);
  }
}
