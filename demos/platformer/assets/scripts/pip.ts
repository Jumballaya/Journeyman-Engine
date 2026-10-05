// Pip: running and jumping through the tile map, bumping blocks, stomping and
// kicking, power-ups, the flagpole, the camera, and dying. Pip decides every
// interaction and tells the other entity through tags ("stomped", "bumped"...).
import { Audio, Entity, Input, Overrides, Sound, World, self, spawn } from "@jm/runtime";
import { Body, GRAVITY } from "./lib/body";
import { levelById } from "./lib/levels";
import { Outcome, Session } from "./lib/session";
import { TILE, TileMap, tileTag } from "./lib/tiles";
import { VIEW_HALF_W, lookAt } from "./lib/view";

const WALK_SPEED: f32 = 90;
const RUN_SPEED: f32 = 150;
const GROUND_ACCEL: f32 = 300;
const AIR_ACCEL: f32 = 200;
const FRICTION: f32 = 360;
const JUMP_SPEED: f32 = 260;
const JUMP_HOLD_GRAVITY: f32 = 0.52;  // while rising with jump held: ~4.5 tiles high
const STOMP_BOUNCE: f32 = 220;
const SMALL_HALF_H: f32 = 8;
const BIG_HALF_H: f32 = 12;

enum Mode { Playing, Dying, Pole, Walking }

const me = self();
const map = new TileMap(levelById(Session.level));
const body = new Body(6, SMALL_HALF_H);

let mode = Mode.Playing;
let modeTime: f32 = 0;
let facing: f32 = 1;
let invulnerable: f32 = 0;
let cameraX: f32 = VIEW_HALF_W;
let flagX: f32 = 1e9;  // the flagpole's x; Pip grabs it on reaching it
let castleX: f32 = 0;
let hopped = false;  // the death hop has started
let shown = "";      // the animation playing

body.x = me.transform.x;
body.y = me.transform.y;
setBig(Session.big);
const flag = World.find("flag");
if (!flag.isNone) {
  const poleX = flag.transform.x + 8;  // the flag hangs left of its pole
  flagX = poleX - 6;
  castleX = poleX + 5 * TILE;          // the castle's door (see level.ts pole())
}

function play(name: string, gain: f32 = 0.6): void { new Sound(name).play(gain); }

function setBig(on: bool): void {
  const halfH = on ? BIG_HALF_H : SMALL_HALF_H;
  body.y += halfH - body.halfH;  // keep the feet where they are
  body.halfH = halfH;
  me.transform.scaleY = halfH;
  me.collider.halfHeight = halfH - 1;
  Session.big = on;
}

function animate(): void {
  const size = Session.big ? "big_" : "small_";
  if (mode == Mode.Dying) playOnce("small_dead");
  else if (!body.onGround && mode != Mode.Walking) playOnce(size + "jump");
  else if (Mathf.abs(body.vx) > 5) playOnce(size + "walk");
  else playOnce(size + "idle");
  me.transform.scaleX = facing * 8;
  const inCastle = mode == Mode.Walking && body.x >= castleX;
  const blink = invulnerable > 0 && <i32>Mathf.floor(invulnerable * 20) % 2 == 0;
  me.sprite.alpha = inCastle ? 0 : blink ? 0.25 : 1;
}

// Sprite.play restarts an animation; only switch when it changes.
function playOnce(animation: string): void {
  if (animation == shown) return;
  shown = animation;
  me.sprite.play(animation);
}

function die(): void {
  if (mode == Mode.Dying) return;
  mode = Mode.Dying;
  modeTime = 0;
  me.collider.layerMask = 0;
  me.collider.collidesWithMask = 0;
  if (Session.big) setBig(false);
  Audio.stopAll(0.05);  // the music stops for the death jingle
  play("die", 0.7);
}

function hurt(): void {
  if (invulnerable > 0 || mode != Mode.Playing) return;
  if (!Session.big) {
    die();
    return;
  }
  setBig(false);
  invulnerable = 2;
  play("shrink");
}

function run(dt: f32): void {
  const input = Input.axis("left", "right");
  const top = Input.down("run") ? RUN_SPEED : WALK_SPEED;
  if (input != 0) {
    facing = input > 0 ? 1 : -1;
    const accel = body.onGround ? GROUND_ACCEL : AIR_ACCEL;
    body.vx = Mathf.max(-top, Mathf.min(top, body.vx + input * accel * dt * (body.vx * input < 0 ? 2 : 1)));
  } else if (body.onGround) {
    body.vx = body.vx > 0 ? Mathf.max(0, body.vx - FRICTION * dt) : Mathf.min(0, body.vx + FRICTION * dt);
  }
  if (Input.pressed("jump") && body.onGround) {
    body.vy = JUMP_SPEED + Mathf.abs(body.vx) * 0.15;
    play(Session.big ? "big_jump" : "jump", 0.4);
  }
  const holding = Input.down("jump") && body.vy > 0;
  body.move(map, dt, holding ? JUMP_HOLD_GRAVITY : 1);
  body.x = Mathf.max(body.x, cameraX - VIEW_HALF_W + body.halfW);  // no going back off-screen

  if (body.hitHeadTile >= 0) bump(body.hitHeadTile, body.hitHeadRow);
  if (body.y < -TILE || body.touchesDeadly(map) || Session.time <= 0) die();
  if (body.x >= flagX) grabPole();
}

function bump(tx: i32, ty: i32): void {
  const block = World.find(tileTag(tx, ty));
  if (!block.isNone && map.bumpable(tx, ty)) {
    block.send(Session.big && map.at(tx, ty) == "B" ? "smash" : "bump");
  }
  play("bump", 0.5);
}

function grabPole(): void {
  mode = Mode.Pole;
  modeTime = 0;
  body.x = flagX;
  body.vx = 0;
  facing = 1;
  const height = body.bottom / TILE;  // higher grabs score more
  Session.addScore(height > 9 ? 5000 : height > 6 ? 2000 : height > 4 ? 800 : 400);
  Audio.stopAll(0.1);
  play("flagpole", 0.6);
}

// Slide down the pole with the flag, then walk into the castle.
function finishLevel(dt: f32): void {
  modeTime += dt;
  if (mode == Mode.Pole) {
    body.vy = -110;
    body.move(map, dt, 0);
    if (!flag.isNone && flag.transform.y > 3 * TILE) flag.transform.y -= 110 * dt;
    if (body.onGround && modeTime > 1.0) {
      mode = Mode.Walking;
      facing = 1;
    }
    return;
  }
  body.vx = 60;
  body.move(map, dt);
  if (body.x >= castleX) Session.outcome = Outcome.Clear;  // inside: animate() hides Pip
}

// A beat of stillness, then a hop up and a fall through the floor.
function fallDead(dt: f32): void {
  modeTime += dt;
  if (modeTime < 0.5) return;
  if (!hopped) {
    hopped = true;
    body.vy = 300;
  }
  body.vy -= GRAVITY * dt;
  body.y += body.vy * dt;
  if (modeTime > 3) Session.outcome = Outcome.Dead;
}

function followCamera(): void {
  cameraX = Mathf.max(cameraX, body.x);
  cameraX = Mathf.min(cameraX, <f32>map.width * TILE - VIEW_HALF_W);
  lookAt(cameraX);
}

export function onUpdate(dt: f32): void {
  if (Session.outcome != Outcome.Playing) {
    animate();
    return;
  }
  invulnerable -= dt;
  if (mode == Mode.Playing) run(dt);
  else if (mode == Mode.Dying) fallDead(dt);
  else finishLevel(dt);

  me.transform.setPosition(body.x, body.y);
  followCamera();
  animate();
}

function stomp(enemy: Entity): void {
  enemy.send("stomp");
  body.vy = Input.down("jump") ? STOMP_BOUNCE * 1.4 : STOMP_BOUNCE;
  Session.addScore(100);
  scorePopup(enemy, 100);
  play("stomp");
}

// The points floating up where they were scored.
function scorePopup(at: Entity, points: i32): void {
  spawn("score_popup", at.transform.x, at.transform.y + 12,
        new Overrides().text("TextComponent", "text", points.toString()));
}

export function onCollide(other: Entity): void {
  if (mode != Mode.Playing) return;
  if (other.hasTag("coin")) {
    play(Session.addCoin() ? "oneup" : "coin", 0.5);
    other.destroy();
  } else if (other.hasTag("mushroom")) {
    other.destroy();
    if (!Session.big) setBig(true);
    Session.addScore(1000);
    play("powerup");
  } else if (other.hasTag("gem")) {
    other.destroy();
    Session.addScore(10000);
    Session.outcome = Outcome.Clear;
  } else if (other.hasTag("enemy")) {
    const falling = body.vy < 0;
    const above = body.bottom > other.transform.y - 2;
    if (falling && above) {
      stomp(other);
    } else if (other.hasTag("shell_idle")) {
      other.send("kick", "", body.x < other.transform.x ? 1 : -1);
      invulnerable = Mathf.max(invulnerable, 0.25);  // don't get hit by the shell we just kicked
      Session.addScore(400);
      scorePopup(other, 400);
    } else {
      hurt();
    }
  }
}
