// Every regular enemy. ScriptComponent params make one script drive fighters,
// zeros, aces, bombers and gunships:
//   hp, score, speed     toughness, reward, cruise speed (px/s)
//   pattern, dir         flight path (see Pattern); dir +1/-1 picks the curve side
//   fire, fireInterval   weapon (see Weapon) and seconds between volleys
//   drop, dropChance     pickup ("power" | "bomb" | "life") and its odds, 0..1
//   big                  1 for large planes
import {
  Audio, Projectile, Timer, Health, HitHistory, Rect, turnTowards, PI, Random, angleTo, Camera, Entity,
  Params, World, self, spawn,
} from "@jm/runtime";
import { HALF_W, HALF_H, DOWN } from "./lib/util";
import { explode } from "./lib/combat";
import * as Session from "./lib/session";

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
me.sprite.shadow({ x: big ? 22 : 14, y: big ? -30 : -20, scale: big ? 0.8 : 0.75, layer: 2, r: 0.02, g: 0.05, b: 0.12, alpha: 0.32 });

const health = new Health(<f32>Params.number("hp", 1));
let t: f32 = 0;
let heading: f32 = pattern == Pattern.Side ? (dir > 0 ? 0 : PI) : DOWN;
let turned: f32 = 0;          // radians turned so far (swoop, loop)
const startX = body.x;
let hoverPhase: i32 = 0;      // 0 descend, 1 hover, 2 leave
let hoverTime: f32 = 0;
const fireTimer = new Timer(fireInterval * Random.range(0.4, 1.0));
let burstLeft: i32 = 0;
const burstTimer = new Timer();
const hitFlash = new Timer();
let seen = false;             // has been on screen (so leaving it means despawn)
let dead = false;
const bombHits = new HitHistory();

function aimAtPlayer(): f32 {
  const player = World.find("player");
  const p = player.transform;
  return player.isAlive && p.y > -1000 ? angleTo(body.x, body.y, p.x, p.y) : DOWN;
}

const firingArea = new Rect(-HALF_W + 10, -HALF_H + 60, HALF_W - 10, HALF_H - 20);
const despawnArea = new Rect(-HALF_W - 80, -HALF_H - 60, HALF_W + 80, HALF_H + 120);
function onScreen(): bool { return firingArea.contains(body.x, body.y); }

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
        heading = turnTowards(heading, aimAtPlayer(), 2.6 * dt);
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

const aimedShot = new Projectile("enemy_bullet", 190);
const straightShot = new Projectile("enemy_bullet", 220);
const spreadShot = new Projectile("enemy_bullet", 180);
const heavyShot = new Projectile("enemy_bullet_big", 150);
const burstShot = new Projectile("enemy_bullet", 210);

function fire(): void {
  const x = body.x;
  const y = body.y - 10;
  switch (weapon) {
    case Weapon.Aimed: aimedShot.fire(x, y, aimAtPlayer()); break;
    case Weapon.Straight: straightShot.fire(x, y, DOWN); break;
    case Weapon.Spread3: spreadShot.fan(x, y, aimAtPlayer(), 3, 0.44); break;
    case Weapon.Spread5: heavyShot.fan(x, y - 6, DOWN, 5, 0.96); break;
    case Weapon.Burst:
      burstLeft = 4;
      burstTimer.start(0);
      return;  // each burst shot plays its own sound
    default: return;
  }
  Audio.play("enemy_shoot", 0.35);
}

function updateWeapon(dt: f32): void {
  if (weapon != Weapon.None && onScreen() && !Session.stageOver.value) {
    fireTimer.tick(dt);
    if (fireTimer.ready) {
      fireTimer.start(fireInterval * Random.range(0.8, 1.2));
      fire();
    }
  }
  if (burstLeft > 0) {
    burstTimer.tick(dt);
    if (burstTimer.ready) {
      burstTimer.start(0.12);
      burstLeft--;
      burstShot.fire(body.x, body.y - 18, aimAtPlayer());
      Audio.play("enemy_shoot", 0.3);
    }
  }
}

function remove(): void {
  dead = true;
  me.destroy();
}

function die(): void {
  explode(body.x, body.y, big);
  Audio.play(big ? "explode_big" : "explode_small", big ? 0.9 : 0.6);
  if (big) Camera.shake(6, 0.35);
  Session.score.add(Params.number("score", 100));
  Session.kills.add(1);
  const drop = Params.text("drop");
  if (drop.length > 0 && Random.chance(<f32>Params.number("dropChance"))) spawn("pickup_" + drop, body.x, body.y);
  remove();
}

function damage(amount: f32): void {
  hitFlash.start(0.07);
  if (health.damage(amount)) die();
  else Audio.play("hit", 0.35);
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
  const gone = seen && !despawnArea.contains(body.x, body.y);
  if (gone || t > 40) {
    remove();
    return;
  }
  updateWeapon(dt);

  hitFlash.tick(dt);
  if (!hitFlash.ready) me.sprite.setColor(1, 0.45, 0.45);
  else me.sprite.setColor(1, 1, 1);
}

export function onCollide(other: Entity): void {
  if (dead) return;
  if (other.hasTag("player_bullet")) {
    other.destroy();
    Session.hits.add(1);
    damage(1);
  } else if (other.hasTag("bomb")) {
    if (bombHits.accept(other)) damage(12);
  } else if (other.hasTag("player")) {
    damage(6);
  }
}
