import { brickPoints, rowAt, brickY, clampPaddle, paddleVx, hitsSide, livesAfterMiss, BALL_SPEED, vyFor } from "../assets/scripts/lib/rules";

export function topRowScoresMost(): void {
  assert(brickPoints(0) > brickPoints(4), "top row worth more");
  assert(brickPoints(4) == 10, "bottom row is 10");
}

export function rowRoundTrips(): void {
  for (let r = 0; r < 5; r++) assert(rowAt(brickY(r)) == r, "row " + r.toString());
}

export function paddleStaysOnScreen(): void {
  assert(clampPaddle(10000) == 580, "right edge");
  assert(clampPaddle(-10000) == -580, "left edge");
  assert(clampPaddle(12) == 12, "inside");
}

export function paddleEdgesAngleTheBall(): void {
  assert(paddleVx(60, 0) > 0, "right edge goes right");
  assert(paddleVx(-60, 0) < 0, "left edge goes left");
  const vx = paddleVx(30, 0);
  const vy = vyFor(vx);
  assert(Mathf.abs(Mathf.sqrt(vx * vx + vy * vy) - BALL_SPEED) < 0.5, "speed constant");
}

export function sideVersusTopHits(): void {
  assert(hitsSide(60, 0, 0, 0, 52, 14), "from the side");
  assert(!hitsSide(0, 20, 0, 0, 52, 14), "from above");
}

export function threeMissesEndTheGame(): void {
  let lives = 3;
  lives = livesAfterMiss(lives);
  lives = livesAfterMiss(lives);
  lives = livesAfterMiss(lives);
  assert(lives == 0, "no lives left");
}
