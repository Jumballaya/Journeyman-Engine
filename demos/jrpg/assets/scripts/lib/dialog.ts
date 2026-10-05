// A dialog box (#dialog in the map UI) that types out lines and can end with
// a choice. The world pauses while it is open, so its owner must be a
// runWhenPaused script.
import { Input, Menu, Sound, Time, UI } from "@jm/runtime";

const CHARS_PER_SECOND: f32 = 40;
const MAX_CHOICES = 4;   // #choice-0 .. #choice-3

export class Dialog {
  // The choice picked when the dialog last closed, or -1 (none offered or cancelled).
  answer: i32 = -1;
  private lines: string[] = [];
  private choices: string[] = [];
  private menu: Menu = new Menu([]);
  private choosing: bool = false;  // the choice menu is up
  private line: i32 = 0;
  private typed: f32 = 0;
  private blip: Sound = new Sound("text");

  get open(): bool { return this.line < this.lines.length; }

  show(lines: string[], choices: string[] = []): void {
    this.lines = lines;
    this.choices = choices;
    this.line = 0;
    this.typed = 0;
    this.answer = -1;
    UI.setVisible("dialog", true, "hidden");
    UI.setVisible("choices", false, "hidden");
    Time.pause();
  }

  // Types and pages with confirm; the last line then waits for a choice.
  // Call every frame with unscaled dt.
  update(dt: f32): void {
    if (!this.open) return;
    const text = this.lines[this.line];
    const before = <i32>this.typed;
    this.typed = Mathf.min(this.typed + CHARS_PER_SECOND * dt, <f32>text.length);
    if (<i32>this.typed != before && before % 2 == 0) this.blip.play(0.3);
    UI.setText("dialog-text", text.substring(0, <i32>this.typed));
    const typedAll = this.typed >= <f32>text.length;
    const last = this.line == this.lines.length - 1;

    if (last && typedAll && this.choices.length > 0) {
      this.choose();
      return;
    }
    if (!Input.pressed("confirm")) return;
    if (!typedAll) this.typed = <f32>text.length;  // first press finishes the line
    else if (last) this.close();
    else {
      this.line++;
      this.typed = 0;
    }
  }

  private choose(): void {
    if (!this.choosing) {  // shown first; input counts from the next frame
      this.choosing = true;
      this.buildMenu();
      return;
    }
    const picked = this.menu.update();
    if (Input.pressed("back")) this.close();
    else if (picked.length > 0) {
      this.answer = this.menu.index;
      this.close();
    }
  }

  private buildMenu(): void {
    UI.setVisible("choices", true, "hidden");
    const ids = new Array<string>();
    for (let i = 0; i < MAX_CHOICES; i++) {
      const id = "choice-" + i.toString();
      const used = i < this.choices.length;
      UI.setVisible(id, used, "hidden");
      if (used) {
        UI.setText(id, this.choices[i]);
        ids.push(id);
      }
    }
    this.menu = new Menu(ids).sounds(new Sound("cursor"), new Sound("confirm"), 0.5, 0.5);
    this.menu.select(0);
  }

  private close(): void {
    this.line = this.lines.length;
    this.choices = [];
    this.choosing = false;
    UI.setVisible("dialog", false, "hidden");
    UI.setVisible("choices", false, "hidden");
    Time.resume();
  }
}
