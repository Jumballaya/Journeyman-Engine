// A lift that goes up and down by itself; whatever stands on it rides.
import { self } from "@jm/runtime";
import { liftVelocity } from "./lib/rules";

const me = self();
let vy: f32 = 0;

export function onUpdate(dt: f32): void {
  vy = liftVelocity(me.transform.y, vy);
  me.velocity.y = vy;   // set every frame: being blocked zeroes it
}
