// A coin: the player takes it, the score goes up, sparkles fly.
import { Entity, GameState, self, spawn } from "@jm/runtime";
import { COIN_VALUE } from "./lib/rules";

const me = self();
let taken = false;

export function onUpdate(dt: f32): void {
  me.transform.rotation += 2 * dt;
}

export function onCollide(other: Entity): void {
  if (taken || !other.hasTag("Player")) return;
  taken = true;
  GameState.add("score", COIN_VALUE);
  spawn("sparkle", me.transform.x, me.transform.y);
  me.destroy();
}
