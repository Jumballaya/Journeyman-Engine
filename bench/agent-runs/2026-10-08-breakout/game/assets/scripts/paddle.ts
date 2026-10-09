import { Input, self } from "@jm/runtime";
import { clampPaddle } from "./lib/rules";

const me = self();
const SPEED: f32 = 600;

export function onUpdate(dt: f32): void {
  me.transform.x = clampPaddle(me.transform.x + Input.axis("left", "right") * SPEED * dt);
}
