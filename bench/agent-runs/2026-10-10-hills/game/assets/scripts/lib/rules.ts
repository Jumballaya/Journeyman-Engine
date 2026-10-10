// Pure game rules, shared by scripts and tests.

export const LIFT_BOTTOM: f32 = -280;
export const LIFT_TOP: f32 = 0;
export const LIFT_SPEED: f32 = 80;
export const COIN_VALUE: f64 = 10;

/** The lift's vertical speed: turns around at either end of its path. */
export function liftVelocity(y: f32, vy: f32): f32 {
  if (y >= LIFT_TOP) return -LIFT_SPEED;
  if (y <= LIFT_BOTTOM) return LIFT_SPEED;
  return vy >= 0 ? LIFT_SPEED : -LIFT_SPEED;
}

/** Jump only from the ground; a jump off a moving lift keeps its speed. */
export function jumpVelocity(onGround: bool, jumpPressed: bool, vy: f32, supportVy: f32): f32 {
  if (onGround && jumpPressed) return 420 + supportVy;
  return vy;
}

export function scoreText(score: f64): string {
  return "SCORE " + (<i32>score).toString();
}
