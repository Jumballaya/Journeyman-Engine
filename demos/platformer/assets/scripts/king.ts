// The Gloop King: hops back and forth across his hall. Three stomps defeat
// him; then the gem (param gx, gy) appears.
import { Camera, Sound, Params, self, spawn } from "@jm/runtime";
import { Walker } from "./lib/walker";
import { Session } from "./lib/session";

const HEALTH = 3;
const HOP_SPEED: f32 = 260;

const me = self();
const walker = new Walker(me, 13, 15, 45);
const hurt = new Sound("boss_hit");
let health = HEALTH;
let stunned: f32 = 0;   // seconds of post-hit invulnerability
let hopTimer: f32 = 1.5;
let t: f32 = 0;

function defeated(): void {
  Session.addScore(5000);
  walker.knockOut();
  Camera.shake(6, 0.8);
  spawn("gem", <f32>Params.number("gx"), <f32>Params.number("gy"));
}

export function onUpdate(dt: f32): void {
  t += dt;
  if (me.hasTag("stomped")) {
    me.removeTag("stomped");
    if (stunned <= 0 && !walker.knockedOut) {
      health--;
      stunned = 1.2;
      hurt.play(0.8);
      Camera.shake(3, 0.3);
      walker.speed += 25;  // angrier with every hit
      if (health == 0) defeated();
    }
  }
  if (!walker.update(dt)) return;
  if (walker.knockedOut) return;

  stunned -= dt;
  me.sprite.alpha = stunned > 0 && <i32>Mathf.floor(t * 16) % 2 == 0 ? 0.3 : 1;
  hopTimer -= dt;
  if (hopTimer <= 0 && walker.body.onGround) {
    hopTimer = 1.2 + <f32>Math.random();
    walker.body.vy = HOP_SPEED;
  }
}
