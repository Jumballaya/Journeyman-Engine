// The ball: serves from the paddle, bounces off walls, paddle and bricks,
// and costs a life when it falls past the paddle.
import { Entity, GameState, Scene, World, self } from "@jm/runtime";
import {
  BALL_HALF, BALL_SPEED, BRICK_HALF_H, BRICK_HALF_W, FIELD_BOTTOM, FIELD_LEFT,
  FIELD_RIGHT, FIELD_TOP, PADDLE_HALF_H, PADDLE_HALF_W, PADDLE_Y,
  flipX, paddleBounceAngle, pointsForBrickY,
} from "./lib/rules";

const SERVE_DELAY: f32 = 0.75;

const me = self();
let vx: f32 = 0;
let vy: f32 = 0;
let served = false;
let serveTimer: f32 = SERVE_DELAY;
let bouncedThisFrame = false;

function followPaddle(): void {
  const paddle = World.find("Paddle");
  if (paddle.isAlive) me.transform.x = paddle.transform.x;
  me.transform.y = PADDLE_Y + PADDLE_HALF_H + BALL_HALF + 2;
}

function launch(): void {
  served = true;
  const angle = Mathf.PI / 3;
  vx = Mathf.cos(angle) * BALL_SPEED;
  vy = Mathf.sin(angle) * BALL_SPEED;
}

function loseLife(): void {
  const lives = <i32>GameState.getNumber("lives", 3) - 1;
  GameState.setNumber("lives", lives);
  if (lives <= 0) {
    Scene.load("gameover");
    return;
  }
  served = false;
  serveTimer = SERVE_DELAY;
  vx = 0;
  vy = 0;
}

export function onUpdate(dt: f32): void {
  bouncedThisFrame = false;
  if (!served) {
    followPaddle();
    serveTimer -= dt;
    if (serveTimer <= 0) launch();
    return;
  }
  let x = me.transform.x + vx * dt;
  let y = me.transform.y + vy * dt;
  if (x < FIELD_LEFT + BALL_HALF) { x = FIELD_LEFT + BALL_HALF; vx = Mathf.abs(vx); }
  if (x > FIELD_RIGHT - BALL_HALF) { x = FIELD_RIGHT - BALL_HALF; vx = -Mathf.abs(vx); }
  if (y > FIELD_TOP - BALL_HALF) { y = FIELD_TOP - BALL_HALF; vy = -Mathf.abs(vy); }
  me.transform.setPosition(x, y);
  if (y < FIELD_BOTTOM - BALL_HALF) loseLife();
}

export function onCollide(other: Entity): void {
  if (!served) return;
  if (other.hasTag("Paddle")) {
    if (vy >= 0) return;
    const angle = paddleBounceAngle(me.transform.x, other.transform.x);
    vx = Mathf.cos(angle) * BALL_SPEED;
    vy = Mathf.sin(angle) * BALL_SPEED;
    me.transform.y = PADDLE_Y + PADDLE_HALF_H + BALL_HALF;
    return;
  }
  if (other.hasTag("brick")) {
    const bx = other.transform.x;
    const by = other.transform.y;
    if (!bouncedThisFrame) {
      bouncedThisFrame = true;
      if (flipX(me.transform.x, me.transform.y, bx, by, BRICK_HALF_W, BRICK_HALF_H)) {
        vx = me.transform.x < bx ? -Mathf.abs(vx) : Mathf.abs(vx);
      } else {
        vy = me.transform.y < by ? -Mathf.abs(vy) : Mathf.abs(vy);
      }
    }
    GameState.add("score", pointsForBrickY(by));
    other.destroy();
  }
}
