import {
  BRICK_COLS, BRICK_HALF_H, BRICK_HALF_W, FIELD_LEFT, FIELD_RIGHT, PADDLE_HALF_W,
  brickX, brickY, clampPaddleX, flipX, paddleBounceAngle, pointsForBrickY,
} from "../assets/scripts/lib/rules";

export function topRowIsWorthMostBottomRowLeast(): void {
  assert(pointsForBrickY(brickY(0)) == 50, "top row scores 50");
  assert(pointsForBrickY(brickY(4)) == 10, "bottom row scores 10");
}

export function brickWallIsCentered(): void {
  const left = brickX(0);
  const right = brickX(BRICK_COLS - 1);
  assert(Mathf.abs(left + right) < 0.001, "the wall is symmetric about x = 0");
  assert(left - BRICK_HALF_W > FIELD_LEFT, "the wall fits inside the field");
}

export function paddleStaysInsideTheField(): void {
  assert(clampPaddleX(10000) == FIELD_RIGHT - PADDLE_HALF_W, "clamped on the right");
  assert(clampPaddleX(-10000) == FIELD_LEFT + PADDLE_HALF_W, "clamped on the left");
  assert(clampPaddleX(12) == 12, "untouched inside");
}

export function paddleCenterSendsBallStraightUp(): void {
  const a = paddleBounceAngle(0, 0);
  assert(Mathf.abs(a - Mathf.PI / 2) < 0.0001, "center: straight up");
  assert(paddleBounceAngle(60, 0) < a, "right side angles right");
  assert(paddleBounceAngle(-60, 0) > a, "left side angles left");
}

export function sideHitFlipsXTopHitFlipsY(): void {
  const side = flipX(-BRICK_HALF_W - 6, 0, 0, 0, BRICK_HALF_W, BRICK_HALF_H);
  const below = flipX(0, -BRICK_HALF_H - 6, 0, 0, BRICK_HALF_W, BRICK_HALF_H);
  assert(side, "a hit on the side flips x");
  assert(!below, "a hit from below flips y");
}
