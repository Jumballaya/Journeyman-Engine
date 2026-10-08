import { self } from "@jm/runtime";

const me = self();
let t: f32 = 0;

// A typical small behavior: a few field reads and writes a frame.
export function onUpdate(dt: f32): void {
  t += dt;
  me.transform.y += Mathf.sin(t) * 0.5;
}
