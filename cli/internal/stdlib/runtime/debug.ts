import { __jmDebugShape, __jmDebugText } from "./env";
import { utf8 } from "./util";

// Shapes and text drawn over the game to see what code is doing: a ray, a
// range, a path, a value over a head. Each lasts this frame (call it every
// onUpdate), or `seconds` of game time; colors are 0xRRGGBB. `Debug` draws in
// world units (y up); `Debug.screen` in screen px from the top-left, like UI.
// Plays, captures and the state dump (`debug`) show them; an exported game
// never draws them (it ignores JM_DEBUG_DRAW).
export class DebugDraw {
  constructor(private onScreen: bool) {}

  line(x1: f32, y1: f32, x2: f32, y2: f32, color: u32 = 0xff00ff, seconds: f32 = 0): void {
    __jmDebugShape(0, this.onScreen, x1, y1, x2, y2, color, seconds);
  }
  circle(x: f32, y: f32, radius: f32, color: u32 = 0xff00ff, seconds: f32 = 0): void {
    __jmDebugShape(1, this.onScreen, x, y, radius, radius, color, seconds);
  }
  // A rectangle's outline, by its center and size.
  rect(x: f32, y: f32, width: f32, height: f32, color: u32 = 0xff00ff, seconds: f32 = 0): void {
    __jmDebugShape(2, this.onScreen, x, y, width, height, color, seconds);
  }
  // Text starting at (x, y), centered on it vertically.
  text(x: f32, y: f32, text: string, color: u32 = 0xffffff, seconds: f32 = 0): void {
    const t = utf8(text);
    __jmDebugText(this.onScreen, x, y, t.dataStart, t.length, color, seconds);
  }

  // The same calls in screen px.
  get screen(): DebugDraw { return screenDraw; }
}

const screenDraw = new DebugDraw(true);
export const Debug = new DebugDraw(false);
