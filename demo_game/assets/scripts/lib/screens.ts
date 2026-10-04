// Menus, visibility and scene changes for the UI screens.
import { Input, Scene, UI } from "@jm/runtime";
import { sfx } from "./util";

// Uses the `.hidden { display: none !important }` class from theme.css.
export function setVisible(id: string, visible: bool): void {
  UI.toggleClass(id, "hidden", !visible);
}

// Moves to another scene with a shader transition (once; repeat calls during
// a transition are ignored).
export function goTo(scene: string, shader: string = "wipe", seconds: f32 = 1.0): void {
  if (!Scene.transitioning) Scene.transition(scene, seconds, shader);
}

// A vertical menu of element ids driven by the up/down/confirm actions; the
// selected element gets the "selected" class.
export class Menu {
  index: i32 = 0;

  constructor(readonly items: string[]) {}

  get selected(): string { return this.items[this.index]; }

  select(index: i32): void {
    this.index = index;
    this.render();
  }

  render(): void {
    for (let i = 0; i < this.items.length; i++) UI.toggleClass(this.items[i], "selected", i == this.index);
  }

  // The chosen item's id on confirm, "" otherwise.
  update(): string {
    const step = (Input.pressed("down") ? 1 : 0) - (Input.pressed("up") ? 1 : 0);
    if (step != 0) {
      this.select((this.index + step + this.items.length) % this.items.length);
      sfx("menu_move", 0.6);
    }
    if (!Input.pressed("confirm")) return "";
    sfx("menu_select", 0.8);
    return this.selected;
  }
}
