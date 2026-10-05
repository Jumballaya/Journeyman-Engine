// A dialog box (#dialog / #dialog-text in area.ui.html) that types out lines.
// The world pauses while it is open, so its owner must be a runWhenPaused script.
import { Input, Sound, Time, UI } from "@jm/runtime";

const CHARS_PER_SECOND: f32 = 40;

export class Dialog {
  private lines: string[] = [];
  private line: i32 = 0;
  private typed: f32 = 0;
  private blip: Sound = new Sound("text");

  get open(): bool { return this.line < this.lines.length; }

  show(lines: string[]): void {
    this.lines = lines;
    this.line = 0;
    this.typed = 0;
    UI.setVisible("dialog", true, "hidden");
    Time.pause();
  }

  // Types and pages through the lines with confirm; call every frame with unscaled dt.
  update(dt: f32): void {
    if (!this.open) return;
    const text = this.lines[this.line];
    const before = <i32>this.typed;
    this.typed = Mathf.min(this.typed + CHARS_PER_SECOND * dt, <f32>text.length);
    if (<i32>this.typed != before && before % 2 == 0) this.blip.play(0.3);
    if (Input.pressed("confirm")) {
      if (this.typed < <f32>text.length) {
        this.typed = <f32>text.length;  // first press finishes the line
      } else if (++this.line < this.lines.length) {
        this.typed = 0;
      } else {
        UI.setVisible("dialog", false, "hidden");
        Time.resume();
        return;
      }
    }
    if (this.open) UI.setText("dialog-text", this.lines[this.line].substring(0, <i32>this.typed));
  }
}
