// The title, game-over and ending screens. Param "items": comma-separated
// menu element ids, each naming its action by suffix (new, continue, title,
// quit); without items, confirm returns to the title. Param "jingle" plays on entry.
import { App, Input, Menu, Music, Params, Scene, Sound, UI } from "@jm/runtime";
import { Party } from "./lib/party";

const ids = Params.text("items").length > 0 ? Params.text("items").split(",") : new Array<string>();
const menu = new Menu(ids).sounds(new Sound("cursor"), new Sound("confirm"), 0.5, 0.5);
const jingle = Params.text("jingle");
let leaving = false;
let t: f32 = 0;

if (jingle.length > 0) new Sound(jingle).play(0.6);
if (Params.text("music").length > 0) new Music(Params.text("music")).play(0.5);
let first = 0;  // start on the first choice that works
for (let i = 0; i < ids.length; i++) {
  const disabled = ids[i].endsWith("continue") && !Party.hasSave;
  UI.toggleClass(ids[i], "disabled", disabled);
  if (disabled && first == i) first = i + 1;
}
UI.setText("lv-kael", Party.level("kael").toString());
UI.setText("lv-lyra", Party.level("lyra").toString());
UI.setText("lv-bram", Party.level("bram").toString());
menu.select(first);

function go(scene: string): void {
  leaving = true;
  Scene.transition(scene, 0.8);
}

export function onUpdate(dt: f32): void {
  t += dt;
  if (leaving || Scene.transitioning || t < 0.5) return;
  if (ids.length == 0) {
    if (Input.justPressed("confirm")) go("title");
    return;
  }
  const choice = menu.update();
  if (choice.endsWith("new")) {
    Party.newGame();
    go(Party.map);
  } else if (choice.endsWith("continue")) {
    if (!Party.hasSave) return;
    Party.load();
    go(Party.map);
  } else if (choice.endsWith("title")) {
    go("title");
  } else if (choice.endsWith("quit")) {
    App.quit();
  }
}
