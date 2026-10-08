// Pure Breakout rules, shared by scripts and tests.

export const HALF_W: f32 = 640;
export const HALF_H: f32 = 360;
export const BALL_SPEED: f32 = 420;
export const BALL_R: f32 = 8;
export const PADDLE_HALF_W: f32 = 60;
export const PADDLE_Y: f32 = -320;
export const START_LIVES: i32 = 3;

export const BRICK_ROWS: i32 = 5;
export const BRICK_COLS: i32 = 10;
export const BRICK_HALF_W: f32 = 52;
export const BRICK_HALF_H: f32 = 14;
export const BRICK_TOP: f32 = 280;

// Points for a brick in a row (row 0 is the top, worth most).
export function brickPoints(row: i32): i32 {
  return (BRICK_ROWS - row) * 10;
}

// Row of a brick from its y position.
export function rowAt(y: f32): i32 {
  return <i32>Mathf.round((BRICK_TOP - y) / (BRICK_HALF_H * 2 + 8));
}

export function brickX(col: i32): f32 {
  return (<f32>col - <f32>(BRICK_COLS - 1) / 2) * (BRICK_HALF_W * 2 + 8);
}

export function brickY(row: i32): f32 {
  return BRICK_TOP - <f32>row * (BRICK_HALF_H * 2 + 8);
}

// Keep the paddle on screen.
export function clampPaddle(x: f32): f32 {
  const limit = HALF_W - PADDLE_HALF_W;
  return Mathf.max(-limit, Mathf.min(limit, x));
}

// Horizontal speed after hitting the paddle: further from the center, steeper.
export function paddleVx(ballX: f32, paddleX: f32): f32 {
  let t = (ballX - paddleX) / PADDLE_HALF_W;
  t = Mathf.max(-1, Mathf.min(1, t));
  return t * BALL_SPEED * 0.75;
}

// Vertical speed that keeps the ball's speed constant for a given vx.
export function vyFor(vx: f32): f32 {
  return Mathf.sqrt(Mathf.max(BALL_SPEED * BALL_SPEED - vx * vx, 100));
}

// True when the ball hit a box on its left/right side rather than top/bottom.
export function hitsSide(bx: f32, by: f32, ox: f32, oy: f32, ohw: f32, ohh: f32): bool {
  const dx = Mathf.abs(bx - ox) - ohw;
  const dy = Mathf.abs(by - oy) - ohh;
  return dx > dy;
}

// Lives after a miss, and whether the game is over.
export function livesAfterMiss(lives: i32): i32 {
  return lives > 0 ? lives - 1 : 0;
}
