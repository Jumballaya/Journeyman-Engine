import { self } from "@jm/runtime";

const me = self();
let frame: i32 = 0;

// A new pixel size every frame: 8 px up to 408 px, then again.
export function onUpdate(dt: f32): void {
  frame++;
  const px: i32 = 8 + (frame % 400);
  me.text.size = <f32>px;
}
