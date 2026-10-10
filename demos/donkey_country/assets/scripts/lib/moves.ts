// How the donkey moves: pure rules, so tests can check them without a game.

export const RUN: f32 = 280;          // top speed, units per second
export const ACCEL: f32 = 2400;       // on the ground; less in the air
export const JUMP: f32 = 900;         // takeoff speed: about 184 units up
export const CUT: f32 = 0.45;         // letting go of jump early keeps this much of the rise
export const COYOTE: f32 = 0.1;       // seconds after leaving a ledge a jump still works
export const BUFFER: f32 = 0.12;      // seconds a jump pressed too early waits for the ground

// The horizontal speed after dt, heading for `axis` (-1..1) times top speed.
export function run(vx: f32, axis: f32, onGround: bool, dt: f32): f32 {
  const want = axis * RUN;
  const step = (onGround ? ACCEL : ACCEL * 0.6) * dt;
  return vx < want ? Mathf.min(want, vx + step) : Mathf.max(want, vx - step);
}

// Whether a jump goes now: pressed within BUFFER, with ground within COYOTE.
export function jumps(sincePressed: f32, sinceGround: f32): bool {
  return sincePressed <= BUFFER && sinceGround <= COYOTE;
}

// Which animation a body moving at (vx, vy) plays.
export function pose(vx: f32, vy: f32, onGround: bool): string {
  if (!onGround) return vy > 0 ? "jump" : "fall";
  return Mathf.abs(vx) > 30 ? "run" : "idle";
}
