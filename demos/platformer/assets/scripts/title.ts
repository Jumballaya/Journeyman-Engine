// Title screen over a sunny backdrop: start a new game or quit.
import { App, Menu, Music, Overrides, Renderer, Scene, Sound, UI, formatNumber, spawn } from "@jm/runtime";
import { Session } from "./lib/session";
import { VIEW_HALF_W, lookAt } from "./lib/view";

const ATLAS = "assets/atlases/sprites.atlas.json#";
const menu = new Menu(["t-start", "t-quit"]).sounds(new Sound("menu"), new Sound("coin"), 0.5, 0.5);
const music = new Music("music_over");
let leaving = false;

Renderer.setClearColor(0.42, 0.62, 0.98);
lookAt(VIEW_HALF_W);
spawn("deco", 40, 40, new Overrides().texture(ATLAS + "hill").scale(24, 8));
spawn("deco", 200, 40, new Overrides().texture(ATLAS + "bush").scale(16, 8));
spawn("deco", 60, 190, new Overrides().texture(ATLAS + "cloud").scale(16, 8));
spawn("deco", 196, 170, new Overrides().texture(ATLAS + "cloud").scale(16, 8));
UI.setText("hiscore", formatNumber(Session.hiscore, 6));
menu.render();
music.play(0.4);

export function onUpdate(dt: f32): void {
  if (leaving) return;
  const choice = menu.update();
  if (choice == "t-start") {
    leaving = true;
    Session.newGame();
    music.fadeOut(0.4);
    Scene.transition("intro", 0.5);
  } else if (choice == "t-quit") {
    App.quit();
  }
}
