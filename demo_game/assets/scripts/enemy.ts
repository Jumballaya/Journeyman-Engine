// Every regular enemy. ScriptComponent params make one script drive fighters,
// zeros, aces, bombers and gunships:
//   hp, score, speed     toughness, reward, cruise speed (px/s)
//   pattern, dir         flight path (see Pattern); dir +1/-1 picks the curve side
//   fire, fireInterval   weapon (see Weapon) and seconds between volleys
//   drop, dropChance     pickup ("power" | "bomb" | "life") and its odds, 0..1
//   big, ship            1 for large planes; atlas region for the shadow
import { Camera, Entity, Params, World, self, spawn } from "@jm/runtime";
import { HALF_W, HALF_H, PI, DOWN, angleTo, rand, sfx } from "./lib/util";
import { Shadow, explode, fan, shoot } from "./lib/combat";
import { Session } from "./lib/session";

enum Pattern { Straight, Sine, Swoop, Loop, Dive, Hover, Side }
enum Weapon { None, Aimed, Straight, Spread3, Spread5, Burst }

function parsePattern(name: string): Pattern {
  if (name == "sine") return Pattern.Sine;
  if (name == "swoop") return Pattern.Swoop;
  if (name == "loop") return Pattern.Loop;
  if (name == "dive") return Pattern.Dive;
  if (name == "hover") return Pattern.Hover;
  if (name == "side") return Pattern.Side;
  return Pattern.Straight;
}

function parseWeapon(name: string): Weapon {
  if (name == "aimed") return Weapon.Aimed;
  if (name == "straight") return Weapon.Straight;
  if (name == "spread3") return Weapon.Spread3;
  if (name == "spread5") return Weapon.Spread5;
  if (name == "burst") return Weapon.Burst;
  return Weapon.None;
}

const me = self();
const body = me.transform;
const pattern = parsePattern(Params.text("pattern", "straight"));
const weapon = parseWeapon(Params.text("fire", "none"));
const dir = <f32>Params.number("dir", 1);
const speed = <f32>Params.number("speed", 150);
const fireInterval = <f32>Params.number("fireInterval", 2);
const big = Params.number("big") > 0;
const shadow = new Shadow(Params.text("ship", "ship_0005"), body.scaleX * (big ? 0.8 : 0.75),
                          big ? 22 : 14, big ? -30 : -20);

let hp = <f32>Params.number("hp", 1);
let t: f32 = 0;
let heading: f32 = pattern == Pattern.Side ? (dir > 0 ? 0 : PI) : DOWN;
let turned: f32 = 0;          // radians turned so far (swoop, loop)
const startX = body.x;
let hoverPhase: i32 = 0;      // 0 descend, 1 hover, 2 leave
let hoverTime: f32 = 0;
let fireTimer = fireInterval * rand(0.4, 1.0);
let burstLeft: i32 = 0;
let burstTimer: f32 = 0;
let hitFlash: f32 = 0;
let seen = false;             // has been on screen (so leaving it means despawn)
let dead = false;
let lastBomb: Entity = Entity.NONE;  // a bomb blast hits once

function aimAtPlayer(): f32 {
  const player = World.find("player");
  const p = player.transform;
  return player.isAlive && p.y > -1000 ? angleTo(body.x, body.y, p.x, p.y) : DOWN;
}

function onScreen(): bool {
  return body.x > -HALF_W + 10 && body.x < HALF_W - 10 && body.y < HALF_H - 20 && body.y > -HALF_H + 60;
}

function turn(rate: f32, limit: f32, dt: f32): void {
  if (turned >= limit) return;
  heading += dir * rate * dt;
  turned += rate * dt;
}

// Updates heading (and sometimes position) for the flight pattern; returns
// this frame's speed along the heading.
function fly(dt: f32): f32 {
  switch (pattern) {
    case Pattern.Sine:
      body.x = startX + <f32>Params.number("amp", 70) * Mathf.sin(t * <f32>Params.number("freq", 2.4));
      return speed;
    case Pattern.Swoop:  // dive in, then curve toward the other side
      if (t > 0.55) turn(1.9, 1.9, dt);
      return speed;
    case Pattern.Loop:
      if (t > 0.8) turn(3.8, PI * 2, dt);
      return speed;
    case Pattern.Dive: {  // home in on the player briefly, then accelerate
      if (t > 0.5 && t < 1.3) {
        let diff = aimAtPlayer() - heading;
        while (diff > PI) diff -= PI * 2;
        while (diff < -PI) diff += PI * 2;
        heading += Mathf.max(-2.6 * dt, Mathf.min(2.6 * dt, diff));
      }
      return t > 0.5 ? speed * 1.7 : speed;
    }
    case Pattern.Hover: return hover(dt);
    case Pattern.Side:
      if (t > 0.6) heading -= dir * 0.35 * dt;  // drift downward
      return speed;
    default:
      return speed;
  }
}

function hover(dt: f32): f32 {
  const hoverY = <f32>Params.number("hoverY", 170);
  if (hoverPhase == 0) {
    if (body.y <= hoverY + 2) hoverPhase = 1;
    return Mathf.max(25, (body.y - hoverY) * 1.6);
  }
  if (hoverPhase == 1) {
    hoverTime += dt;
    body.x += Mathf.sin(hoverTime * 0.9) * 30 * dt;
    if (hoverTime > <f32>Params.number("hoverTime", 7)) hoverPhase = 2;
    return 0;
  }
  return 160;  // done hovering: leave through the bottom
}

function fire(): void {
  const x = body.x;
  const y = body.y - 10;
  switch (weapon) {
    case Weapon.Aimed: shoot("enemy_bullet", x, y, aimAtPlayer(), 190); break;
    case Weapon.Straight: shoot("enemy_bullet", x, y, DOWN, 220); break;
    case Weapon.Spread3: fan("enemy_bullet", x, y, aimAtPlayer(), 0.44, 3, 180); break;
    case Weapon.Spread5: fan("enemy_bullet_big", x, y - 6, DOWN, 0.96, 5, 150); break;
    case Weapon.Burst:
      burstLeft = 4;
      burstTimer = 0;
      return;  // each burst shot plays its own sound
    default: return;
  }
  sfx("enemy_shoot", 0.35);
}

function updateWeapon(dt: f32): void {
  if (weapon != Weapon.None && onScreen() && !Session.stageOver) {
    fireTimer -= dt;
    if (fireTimer <= 0) {
      fireTimer = fireInterval * rand(0.8, 1.2);
      fire();
    }
  }
  if (burstLeft > 0) {
    burstTimer -= dt;
    if (burstTimer <= 0) {
      burstTimer = 0.12;
      burstLeft--;
      shoot("enemy_bullet", body.x, body.y - 18, aimAtPlayer(), 210);
      sfx("enemy_shoot", 0.3);
    }
  }
}

function remove(): void {
  dead = true;
  shadow.destroy();
  me.destroy();
}

function die(): void {
  explode(body.x, body.y, big);
  sfx(big ? "explode_big" : "explode_small", big ? 0.9 : 0.6);
  if (big) Camera.shake(6, 0.35);
  Session.addScore(Params.number("score", 100));
  Session.countKill();
  const drop = Params.text("drop");
  if (drop.length > 0 && Math.random() < Params.number("dropChance")) spawn("pickup_" + drop, body.x, body.y);
  remove();
}

function damage(amount: f32): void {
  hp -= amount;
  hitFlash = 0.07;
  if (hp <= 0) die();
  else sfx("hit", 0.35);
}

export function onUpdate(dt: f32): void {
  if (dead) return;
  t += dt;

  const s = fly(dt);
  if (pattern != Pattern.Sine) {
    body.x += Mathf.cos(heading) * s * dt;
    body.y += Mathf.sin(heading) * s * dt;
  } else {
    body.y -= s * dt;
  }
  body.rotation = heading - PI / 2;

  if (onScreen()) seen = true;
  const gone = seen && (body.y < -HALF_H - 60 || body.y > HALF_H + 120 || Mathf.abs(body.x) > HALF_W + 80);
  if (gone || t > 40) {
    remove();
    return;
  }
  shadow.follow(body);
  updateWeapon(dt);

  hitFlash -= dt;
  if (hitFlash > 0) me.sprite.setColor(1, 0.45, 0.45);
  else me.sprite.setColor(1, 1, 1);
}

export function onCollide(other: Entity): void {
  if (dead) return;
  if (other.hasTag("player_bullet")) {
    other.destroy();
    Session.countHit();
    damage(1);
  } else if (other.hasTag("bomb")) {
    if (!other.equals(lastBomb)) {
      lastBomb = other;
      damage(12);
    }
  } else if (other.hasTag("player")) {
    damage(6);
  }
}
