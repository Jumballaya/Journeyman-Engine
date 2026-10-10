import { Physics, self } from "@jm/runtime";

// Walks back and forth: turns at walls and at the edges of what it walks on.
const me = self();
let dir: f32 = -1;
const SPEED: f32 = 70;

export function onUpdate(dt: f32): void {
  const v = me.velocity;
  if (v.blockedX != 0) dir = -dir;
  if (v.onGround) {  // the ground a step ahead, or turn back
    const ahead = Physics.raycast(me.transform.x + dir * 22, me.transform.y, 0, -1, 40, me);
    if (ahead === null) dir = -dir;
  }
  v.x = dir * SPEED;
  me.transform.setScale(dir > 0 ? 24 : -24, 16);
}
