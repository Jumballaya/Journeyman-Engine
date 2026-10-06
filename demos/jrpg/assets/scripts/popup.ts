// A damage number: drifts up (its velocity) and fades before its lifetime ends.
import { self } from "@jm/runtime";

const me = self();
let age: f32 = 0;

export function onUpdate(dt: f32): void {
  age += dt;
  me.text.alpha = 1 - Mathf.max(0, age - 0.5) / 0.4;
}
