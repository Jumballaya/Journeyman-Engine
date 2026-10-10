// A player's blob, simulated on its player's machine (Network authority
// "owner"): moving it feels instant there, and everyone else sees it follow.
import { Entity, GameState, Input, Vec2, clamp, self } from "@jm/runtime";
import { PHASE, red, green, blue } from "./lib/players";

const SPEED: f32 = 165;
const me = self();
const body = me.transform;
const move = new Vec2();
let wobble: f32 = 0;

me.sprite.setColor(red(me.owner), green(me.owner), blue(me.owner));  // replicated: SpriteComponent

export function onUpdate(dt: f32): void {
  if (GameState.getString(PHASE) == "results") return;
  Input.vector("left", "right", "down", "up", move);
  body.x = clamp(body.x + move.x * SPEED * dt, -290, 290);
  body.y = clamp(body.y + move.y * SPEED * dt, -152, 124);
  // Squash a little while moving.
  const moving = move.length > 0;
  wobble += dt * (moving ? 14 : 4);
  const squash: f32 = Mathf.sin(wobble) * (moving ? 1.4 : 0.25);
  body.scaleX = 12 + squash;
  body.scaleY = 12 - squash;
}

// On this machine only: tell the pellet's host we got it, once (it stays
// here until the host's word that it's gone arrives).
const asked: Entity[] = [];
export function onOverlap(other: Entity): void {
  if (!other.hasTag("pellet")) return;
  for (let i = 0; i < asked.length; i++) {
    if (asked[i].index == other.index && asked[i].generation == other.generation) return;
  }
  if (asked.length >= 8) asked.shift();
  asked.push(other);
  other.send("eat");
}
