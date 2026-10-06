// The title screen: continue, a new game, load a slot, quit. Runs on the
// title scene's screen entity (title.ui.html).
import { App, GameState, Input, Music, Scene, Sound, UI } from "@jm/runtime";
import * as hero from "./lib/state";
import * as slots from "./lib/slots";
import { Pointer } from "./lib/pointer";

const music = new Music("music_title");
const move = new Sound("ui_move"), select = new Sound("ui_select"), back = new Sound("ui_back");
const ITEMS = ["continue", "new", "load", "quit"];
let row = 0;
let picking = false;  // the slot list is up
let slot = 0;
let leaving = false;

music.play(0.5);
row = slots.latest() > 0 ? 0 : 1;
draw();

function draw(): void {
  UI.toggleClass("continue", "disabled", slots.latest() == 0);
  UI.toggleClass("load", "disabled", !slots.anyUsed());
  for (let i = 0; i < ITEMS.length; i++) UI.toggleClass(ITEMS[i], "selected", i == row && !picking);
  UI.setVisible("slots", picking, "hidden");
  for (let s = 1; s <= slots.SLOTS; s++) {
    const sum = slots.summary(s);
    UI.setText("slot-" + s.toString(), "Slot " + s.toString() + "   " + (sum.used ? sum.place + "   LV " + sum.level.toString() + "   " + slots.clock(sum.seconds) : "empty"));
    UI.toggleClass("slot-" + s.toString(), "selected", picking && s - 1 == slot);
    UI.toggleClass("slot-" + s.toString(), "disabled", !sum.used);
  }
}

function go(scene: string): void {
  leaving = true;
  select.play(0.6);
  music.fadeOut(0.8);
  Scene.transition(scene, 0.8);
}

function startNew(): void {
  hero.newGame();
  GameState.setString("arrive", "start");
  go("intro");
}

function load(s: i32): void {
  if (!slots.load(s)) { back.play(0.5); return; }
  go(GameState.getString("pos.scene", "cinderwell"));
}

export function onUpdate(dt: f32): void {
  if (leaving) return;
  Pointer.update();
  if (picking) {
    const hoveredSlot = Pointer.overRow("slot-", slots.SLOTS);
    if (Pointer.moved && hoveredSlot >= 0 && hoveredSlot != slot) { slot = hoveredSlot; move.play(0.3); draw(); }
    if (Pointer.clicked && hoveredSlot >= 0) { load(hoveredSlot + 1); return; }
    if (Input.pressed("back") || Pointer.rightClicked) { picking = false; back.play(0.5); draw(); return; }
    if (Input.repeated("up", 0.3, 0.15)) { slot = (slot + slots.SLOTS - 1) % slots.SLOTS; move.play(0.4); draw(); }
    if (Input.repeated("down", 0.3, 0.15)) { slot = (slot + 1) % slots.SLOTS; move.play(0.4); draw(); }
    if (Input.pressed("confirm")) load(slot + 1);
    return;
  }
  if (Input.repeated("up", 0.3, 0.15)) { row = (row + ITEMS.length - 1) % ITEMS.length; move.play(0.4); draw(); }
  if (Input.repeated("down", 0.3, 0.15)) { row = (row + 1) % ITEMS.length; move.play(0.4); draw(); }
  let hovered = -1;
  for (let i = 0; i < ITEMS.length; i++) if (Pointer.over(ITEMS[i])) hovered = i;
  if (Pointer.moved && hovered >= 0 && hovered != row) { row = hovered; move.play(0.3); draw(); }
  if (!Input.pressed("confirm") && !(Pointer.clicked && hovered >= 0)) return;
  const item = ITEMS[row];
  if (item == "continue") { if (slots.latest() > 0) load(slots.latest()); else back.play(0.5); }
  else if (item == "new") startNew();
  else if (item == "load") { if (slots.anyUsed()) { picking = true; slot = max(0, slots.latest() - 1); select.play(0.5); draw(); } else back.play(0.5); }
  else App.quit();
}
