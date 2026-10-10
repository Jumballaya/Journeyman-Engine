import { Params, Physics, self } from "@jm/runtime";

// Walks back and forth around where it starts (within `range`): turns there, at walls and at ledges.
const me = self();
let dir: f32 = -1;
const SPEED: f32 = 70;
const home = me.transform.x;
const range = <f32>Params.number("range", 160);

export function onUpdate(dt: f32): void {
  const v = me.velocity;
  if (v.blockedX != 0 || (me.transform.x - home) * dir > range) dir = -dir;
  if (v.onGround) {  // the ground a step ahead, or turn back
    const ahead = Physics.raycast(me.transform.x + dir * 22, me.transform.y, 0, -1, 40, me);
    if (ahead === null) dir = -dir;
  }
  v.x = dir * SPEED;
  me.transform.setScale(dir > 0 ? 24 : -24, 16);
}
