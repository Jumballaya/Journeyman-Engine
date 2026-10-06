// The opening: three pictures with a few lines each (intro.ui.html holds
// them as panels intro-1..3), confirm to go on, back to skip. Then Cinderwell.
import { Input, Music, Scene, Sound, UI } from "@jm/runtime";

const PAGES = 3;
const LINES = [
  "Forty years ago the Foundry at Slagmouth fed every town on the Ash Road. Then its furnaces went cold, and its Warden did not.",
  "Cinderwell held on. Caravans came east through the foundry road, carrying salt, seed and news. Last spring the Warden woke, and the caravans stopped.",
  "You walked in off the ash with a wrench, a name nobody asked for, and nowhere else to be.",
];
const music = new Music("music_road");
const next = new Sound("ui_select");
let page = 0;
let shown: f32 = 0;
let leaving = false;

music.play(0.4);
show();

function show(): void {
  for (let i = 1; i <= PAGES; i++) UI.setVisible("intro-" + i.toString(), i == page + 1, "hidden");
  shown = 0;
  UI.setText("intro-text", "");
  UI.setText("intro-page", (page + 1).toString() + " / " + PAGES.toString());
}

function finish(): void {
  leaving = true;
  music.fadeOut(1);
  Scene.transition("cinderwell", 1.2);
}

export function onUpdate(dt: f32): void {
  if (leaving) return;
  const text = LINES[page];
  if (shown < <f32>text.length) {
    shown += dt * 45;
    UI.setText("intro-text", text.substring(0, min(<i32>shown, text.length)));
    if (Input.pressed("confirm")) { shown = <f32>text.length; UI.setText("intro-text", text); }
    if (Input.pressed("back")) finish();
    return;
  }
  if (Input.pressed("confirm")) {
    next.play(0.5);
    if (++page >= PAGES) finish(); else show();
  } else if (Input.pressed("back")) finish();
}
