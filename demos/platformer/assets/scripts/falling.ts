// Gravity for effects (debris, popped coins) that move by VelocityComponent.
import { self } from "@jm/runtime";
import { GRAVITY } from "./lib/body";

const me = self();

export function onUpdate(dt: f32): void {
  me.velocity.y -= GRAVITY * dt;
}
