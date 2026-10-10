// Title screen: start, starting level (left/right), quit; blocks drift past.
import {
  App, GameState, Input, Menu, Music, Overrides, Random, Save, Scene, Sound, Timer, UI, Window, formatNumber, spawn,
} from "@jm/runtime";
import { KIND_COUNT, red, green, blue } from "./lib/pieces";

const MAX_LEVEL = 15;

const menu = new Menu(["t-start", "t-level", "t-quit"])
  .sounds(new Sound("menu_move"), new Sound("menu_select"), 0.6, 0.8);
const tick = new Sound("move");
const music = new Music("music");
const driftTimer = new Timer();
let level = <i32>GameState.getNumber("startLevel", 1);
let leaving = false;

UI.setText("hiscore", formatNumber(Save.getNumber("hiscore"), 7));
UI.setText("level-value", level.toString());
menu.render();
music.play(0.35);

function drift(): void {
  const kind = Random.int(0, KIND_COUNT - 1);
  spawn("falling_cell", <f32>Random.int(-8, 7) * 30 + 15, 340,
        new Overrides().tint(red(kind), green(kind), blue(kind), 0.22).velocity(0, -Random.range(40, 90)));
}

export function onUpdate(dt: f32): void {
  if (Input.justPressed("fullscreen")) Window.fullscreen = !Window.fullscreen;
  if (driftTimer.tick(dt) || driftTimer.ready) {
    driftTimer.start(Random.range(0.15, 0.4));
    drift();
  }
  if (leaving) return;

  if (menu.selected == "t-level") {
    const step = (Input.justPressed("right") ? 1 : 0) - (Input.justPressed("left") ? 1 : 0);
    if (step != 0) {
      level = (level - 1 + step + MAX_LEVEL) % MAX_LEVEL + 1;
      UI.setText("level-value", level.toString());
      tick.play(0.5);
    }
  }
  const choice = menu.update();
  if (choice == "t-start" || choice == "t-level") {
    leaving = true;
    GameState.setNumber("startLevel", level);
    music.fadeOut(0.5);
    Scene.transition("game", 0.6);
  } else if (choice == "t-quit") {
    App.quit();
  }
}
