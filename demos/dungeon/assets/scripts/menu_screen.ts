// The title and game-over screens: a menu (param "items", comma-separated
// element ids) where each id names its action: start, continue, title, quit.
import { App, Menu, Params, Scene, Sound } from "@jm/runtime";
import { Arrival, Session } from "./lib/session";

const menu = new Menu(Params.text("items").split(",")).sounds(new Sound("menu"), new Sound("gem"), 0.5, 0.5);
let leaving = false;
menu.render();

function go(scene: string): void {
  leaving = true;
  Scene.transition(scene, 0.6);
}

export function onUpdate(dt: f32): void {
  if (leaving) return;
  const choice = menu.update();
  if (choice.endsWith("start")) {
    Session.newGame();
    go("area");
  } else if (choice.endsWith("continue")) {
    Session.health = 6;  // three hearts, from where the area begins
    Session.arrival = Arrival.Start;
    go("area");
  } else if (choice.endsWith("title")) {
    go("title");
  } else if (choice.endsWith("quit")) {
    App.quit();
  }
}
