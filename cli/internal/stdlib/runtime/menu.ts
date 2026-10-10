import { UI } from "./ui";
import { Input } from "./input";
import { Sound } from "./audio";

// Selection, input and highlighting for a list of UI element IDs. Activate only
// the visible menu by calling update() on it. Choices remain game decisions.
export class Menu {
  private selection: i32 = 0;
  private readonly items: string[];
  previousAction: string = "up";
  nextAction: string = "down";
  confirmAction: string = "confirm";
  selectedClass: string = "selected";
  private moveSound: Sound | null = null;
  private confirmSound: Sound | null = null;
  private moveGain: f32 = 1;
  private confirmGain: f32 = 1;

  constructor(items: string[]) { this.items = items.slice(); }
  get index(): i32 { return this.items.length == 0 ? -1 : this.selection; }
  set index(value: i32) {
    this.selection = this.items.length == 0 ? 0 : max(0, min(value, this.items.length - 1));
  }
  get selected(): string { return this.items.length == 0 ? "" : this.items[this.selection]; }
  select(index: i32): void { this.index = index; this.render(); }
  sounds(move: Sound, confirm: Sound, moveGain: f32 = 1, confirmGain: f32 = 1): Menu {
    this.moveSound = move; this.confirmSound = confirm;
    this.moveGain = moveGain; this.confirmGain = confirmGain;
    return this;
  }
  render(): void {
    for (let i = 0; i < this.items.length; i++) UI.toggleClass(this.items[i], this.selectedClass, i == this.selection);
  }
  // Returns the confirmed element ID, or an empty string. Opposite directions cancel each other.
  update(): string {
    return this.handle(Input.justPressed(this.previousAction), Input.justPressed(this.nextAction), Input.justPressed(this.confirmAction));
  }
  // Also useful for callers with their own input source.
  handle(previous: bool, next: bool, confirm: bool): string {
    if (this.items.length == 0) return "";
    if (previous != next) {
      this.selection = (this.selection + this.items.length + (next ? 1 : -1)) % this.items.length;
      this.render();
      const sound = this.moveSound;
      if (sound !== null) sound.play(this.moveGain);
    }
    if (!confirm) return "";
    const sound = this.confirmSound;
    if (sound !== null) sound.play(this.confirmGain);
    return this.selected;
  }
}
