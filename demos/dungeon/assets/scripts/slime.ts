// A slime: hops a short way in a random direction, rests, repeats. One hit.
import { Entity, Random } from "@jm/runtime";
import { Foe } from "./lib/foe";

const HOP_SPEED: f32 = 50;

const foe = new Foe(1, 6);
let hop: f32 = Random.range(0.2, 1.0);  // seconds left in this hop or rest
let hopping = false;
let dx: f32 = 0, dy: f32 = 0;

export function onUpdate(dt: f32): void {
  hop -= dt;
  if (hop <= 0) {
    hopping = !hopping;
    hop = hopping ? Random.range(0.4, 0.8) : Random.range(0.3, 1.0);
    const angle = Random.range(0, Mathf.PI * 2);
    dx = Mathf.cos(angle);
    dy = Mathf.sin(angle);
  }
  const speed = hopping ? HOP_SPEED : 0;
  foe.update(dt, dx * speed * dt, dy * speed * dt);
}

export function onCollide(other: Entity): void {
  if (other.hasTag("sword")) foe.hitBy(other);
}
