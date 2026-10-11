// Spawns `prefab` in waves: `count` every `interval` seconds (the first after
// `delay`), anywhere in a `width` x `height` area around it; `waves` of them
// (0: on and on), with at most `alive` of its own out at once (0: no limit).
import { Entity, Params, Random, self, spawn } from "@jm/runtime";

const me = self();
const prefab = Params.text("prefab", "");
const interval = <f32>Math.max(0.05, Params.number("interval", 2));
const count = <i32>Params.number("count", 1);
const waves = <i32>Params.number("waves", 0);
const alive = <i32>Params.number("alive", 0);
const width = <f32>Params.number("width", 0);
const height = <f32>Params.number("height", 0);
let wait = <f32>Params.number("delay", 0);
let done = 0;
const out: Entity[] = [];

export function onUpdate(dt: f32): void {
  if (prefab == "" || (waves > 0 && done >= waves)) return;
  wait -= dt;
  if (wait > 0) return;
  wait += interval;
  for (let i = out.length - 1; i >= 0; i--) if (!out[i].isAlive) out.splice(i, 1);
  if (alive > 0 && out.length >= alive) return;  // full: this wave waits for room
  done++;
  for (let i = 0; i < count && (alive <= 0 || out.length < alive); i++) {
    const x = me.transform.x + Random.range(-width / 2, width / 2);
    const y = me.transform.y + Random.range(-height / 2, height / 2);
    out.push(spawn(prefab, x, y));
  }
}
