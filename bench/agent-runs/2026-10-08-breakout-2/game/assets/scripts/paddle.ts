// The paddle: arrow keys move it along the bottom of the field.
import { Input, self } from "@jm/runtime";
import { PADDLE_SPEED, PADDLE_Y, clampPaddleX } from "./lib/rules";

const me = self();

export function onUpdate(dt: f32): void {
  const x = me.transform.x + Input.axis("left", "right") * PADDLE_SPEED * dt;
  me.transform.setPosition(clampPaddleX(x), PADDLE_Y);
}
