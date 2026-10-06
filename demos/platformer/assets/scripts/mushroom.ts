// A mushroom rising out of its block, then sliding along the ground until Pip takes it.
import { self } from "@jm/runtime";
import { Walker } from "./lib/walker";
import { TILE } from "./lib/tiles";

const RISE_SECONDS: f32 = 0.7;

const me = self();
const startY = me.transform.y - TILE;  // emerges from inside the block below
const walker = new Walker(me, 7, 8, 50, false);
let rise: f32 = 0;
walker.direction = 1;

export function onUpdate(dt: f32): void {
  if (rise < RISE_SECONDS) {
    rise += dt;
    me.transform.y = startY + TILE * Mathf.min(rise / RISE_SECONDS, 1);
    walker.body.y = me.transform.y;
    return;
  }
  walker.update(dt);
}
