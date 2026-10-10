// A bat: flutters in wobbly curves over anything, darting faster now and then.
import { Entity, Random } from "@jm/runtime";
import { Foe } from "./lib/foe";

const foe = new Foe(1, 6, true);
let heading: f32 = Random.range(0, Mathf.PI * 2);
let turn: f32 = 0;
let speed: f32 = 40;
let change: f32 = 0;

export function onUpdate(dt: f32): void {
  change -= dt;
  if (change <= 0) {
    change = Random.range(0.5, 1.5);
    turn = Random.range(-3, 3);
    speed = Random.chance(0.3) ? 110 : 45;
  }
  heading += turn * dt;
  foe.update(dt, Mathf.cos(heading) * speed * dt, Mathf.sin(heading) * speed * dt);
}

export function onOverlap(other: Entity): void {
  if (other.hasTag("sword")) foe.hitBy(other);
}
