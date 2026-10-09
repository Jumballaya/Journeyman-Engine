// Pure Breakout rules: no engine calls, so tests can exercise them.

export const FIELD_LEFT: f32 = -620;
export const FIELD_RIGHT: f32 = 620;
export const FIELD_TOP: f32 = 330;
export const FIELD_BOTTOM: f32 = -360;

export const PADDLE_Y: f32 = -300;
export const PADDLE_HALF_W: f32 = 70;
export const PADDLE_HALF_H: f32 = 10;
export const PADDLE_SPEED: f32 = 600;

export const BALL_HALF: f32 = 8;
export const BALL_SPEED: f32 = 420;

export const BRICK_COLS: i32 = 10;
export const BRICK_ROWS: i32 = 5;
export const BRICK_HALF_W: f32 = 48;
export const BRICK_HALF_H: f32 = 12;
export const BRICK_TOP_Y: f32 = 260;
export const BRICK_STEP_X: f32 = 104;
export const BRICK_STEP_Y: f32 = 32;

export const START_LIVES: i32 = 3;

export function brickX(col: i32): f32 {
  return -(<f32>(BRICK_COLS - 1)) * BRICK_STEP_X / 2 + <f32>col * BRICK_STEP_X;
}

export function brickY(row: i32): f32 {
  return BRICK_TOP_Y - <f32>row * BRICK_STEP_Y;
}

// Higher rows are worth more: the top row 50, the bottom row 10.
export function pointsForBrickY(y: f32): i32 {
  const row = <i32>Mathf.round((BRICK_TOP_Y - y) / BRICK_STEP_Y);
  const clamped = row < 0 ? 0 : (row >= BRICK_ROWS ? BRICK_ROWS - 1 : row);
  return (BRICK_ROWS - clamped) * 10;
}

// Keeps the paddle's center inside the field.
export function clampPaddleX(x: f32): f32 {
  const lo = FIELD_LEFT + PADDLE_HALF_W;
  const hi = FIELD_RIGHT - PADDLE_HALF_W;
  return x < lo ? lo : (x > hi ? hi : x);
}

// Angle (radians from +x) the ball leaves the paddle at: straight up at the
// center, up to 60 degrees off vertical at the edges.
export function paddleBounceAngle(ballX: f32, paddleX: f32): f32 {
  let t = (ballX - paddleX) / PADDLE_HALF_W;
  if (t < -1) t = -1;
  if (t > 1) t = 1;
  return Mathf.PI / 2 - t * (Mathf.PI / 3);
}

// Which axis to flip when the ball overlaps a box: the one with the smaller
// penetration. Returns true to flip x, false to flip y.
export function flipX(ballX: f32, ballY: f32, boxX: f32, boxY: f32, boxHalfW: f32, boxHalfH: f32): bool {
  const overlapX = BALL_HALF + boxHalfW - Mathf.abs(ballX - boxX);
  const overlapY = BALL_HALF + boxHalfH - Mathf.abs(ballY - boxY);
  return overlapX < overlapY;
}
