import { liftVelocity, jumpVelocity, scoreText, LIFT_TOP, LIFT_BOTTOM, LIFT_SPEED } from "../assets/scripts/lib/rules";

export function liftTurnsAtBothEnds(): void {
  assert(liftVelocity(LIFT_TOP + 1, LIFT_SPEED) == -LIFT_SPEED, "turns down at the top");
  assert(liftVelocity(LIFT_BOTTOM - 1, -LIFT_SPEED) == LIFT_SPEED, "turns up at the bottom");
  const mid: f32 = (LIFT_TOP + LIFT_BOTTOM) / 2;
  assert(liftVelocity(mid, -LIFT_SPEED) == -LIFT_SPEED, "keeps going down mid-path");
  assert(liftVelocity(mid, 0) == LIFT_SPEED, "starts going up");
}

export function jumpsOnlyWhenStanding(): void {
  const vy: f32 = -50;
  assert(jumpVelocity(false, true, vy, 0) == vy, "no jump in the air");
  assert(jumpVelocity(true, false, 0, 0) == 0, "no jump without the key");
  assert(jumpVelocity(true, true, 0, 80) == 500, "jump adds the lift's speed");
}

export function scoreShowsAsText(): void {
  assert(scoreText(30) == "SCORE 30", "score text");
}
