import { Entity, GameState, Scene, UI, World, self } from "@jm/runtime";
import {
  BALL_R, BALL_SPEED, HALF_H, HALF_W, PADDLE_Y, brickPoints, hitsSide,
  livesAfterMiss, paddleVx, rowAt, vyFor,
} from "./lib/rules";

const me = self();
let serving = true;
let serveTime: f32 = 0;

function paddle(): Entity {
  return World.find("Paddle");
}

function showHud(): void {
  UI.setText("score", (<i32>GameState.getNumber("score", 0)).toString());
  UI.setText("lives", (<i32>GameState.getNumber("lives", 3)).toString());
}

showHud();

export function onUpdate(dt: f32): void {
  if (serving) {
    const p = paddle();
    me.transform.setPosition(p.transform.x, PADDLE_Y + 24);
    me.velocity.set(0, 0);
    serveTime += dt;
    if (serveTime > 1.0) {
      serving = false;
      const vx: f32 = BALL_SPEED * 0.4;
      me.velocity.set(vx, vyFor(vx));
    }
    return;
  }
  const x = me.transform.x, y = me.transform.y;
  if (x < -HALF_W + BALL_R) me.velocity.x = Mathf.abs(me.velocity.x);
  if (x > HALF_W - BALL_R) me.velocity.x = -Mathf.abs(me.velocity.x);
  if (y > HALF_H - BALL_R) me.velocity.y = -Mathf.abs(me.velocity.y);
  if (y < -HALF_H - 20) miss();
}

function miss(): void {
  const lives = livesAfterMiss(<i32>GameState.getNumber("lives", 3));
  GameState.setNumber("lives", lives);
  showHud();
  if (lives <= 0) {
    Scene.load("gameover");
    return;
  }
  serving = true;
  serveTime = 0;
}

export function onCollide(other: Entity): void {
  if (serving) return;
  const t = other.transform;
  if (other.hasTag("Paddle")) {
    if (me.velocity.y > 0) return;
    const vx = paddleVx(me.transform.x, t.x);
    me.velocity.set(vx, vyFor(vx));
    return;
  }
  if (other.hasTag("brick")) {
    if (!other.isAlive) return;
    if (hitsSide(me.transform.x, me.transform.y, t.x, t.y, other.collider.halfWidth, other.collider.halfHeight)) {
      me.velocity.x = me.transform.x < t.x ? -Mathf.abs(me.velocity.x) : Mathf.abs(me.velocity.x);
    } else {
      me.velocity.y = me.transform.y < t.y ? -Mathf.abs(me.velocity.y) : Mathf.abs(me.velocity.y);
    }
    GameState.add("score", brickPoints(rowAt(t.y)));
    other.destroy();
    showHud();
  }
}
