// Every regular enemy. Behaviour comes from ScriptComponent params so one
// script drives fighters, zeros, aces, bombers and gunships:
//   hp, score, speed           toughness, reward, cruise speed (px/s)
//   pattern                    straight | sine | swoop | loop | dive | hover | side
//   dir                        +1 / -1: which way swoops/loops/sides curve
//   fire, fireInterval         none | aimed | straight | spread3 | spread5 | burst
//   drop, dropChance           power | bomb | life, 0..1
//   big                        1 for large planes (bigger explosion, shake)
import { Entity, World, GameState, Params, Camera, TransformComponent, SpriteComponent } from "@jm/runtime";
import { HALF_W, HALF_H, PI, rand, sfx, shoot, angleTo, explode, addScore, Shadow } from "./lib/game";

const tr = new TransformComponent();
const sprite = new SpriteComponent();
const playerTr = new TransformComponent();

let initialized = false;
let hp: f32 = 1;
let speed: f32 = 150;
let pattern = "straight";
let dir: f32 = 1;
let fireMode = "none";
let fireInterval: f32 = 2;
let fireTimer: f32 = 0;
let burstLeft: i32 = 0;
let burstTimer: f32 = 0;
let big = false;

let t: f32 = 0;
let heading: f32 = -PI / 2;  // radians; -PI/2 = straight down
let turned: f32 = 0;
let x0: f32 = 0;
let hoverPhase: i32 = 0;     // 0 descend, 1 hover, 2 leave
let hoverTime: f32 = 0;
let seen = false;
let flash: f32 = 0;
let lastBomb: u32 = 0xFFFFFFFF;
let dead = false;
let shadow: Shadow | null = null;

function init(): void {
  initialized = true;
  hp = <f32>Params.number("hp", 1);
  speed = <f32>Params.number("speed", 150);
  pattern = Params.string("pattern", "straight");
  dir = <f32>Params.number("dir", 1);
  fireMode = Params.string("fire", "none");
  fireInterval = <f32>Params.number("fireInterval", 2);
  big = Params.number("big", 0) > 0;
  fireTimer = fireInterval * rand(0.4, 1.0);
  tr.read();
  shadow = big ? new Shadow(Params.string("ship", "ship_0005"), tr.sx * 0.8, 22, -30)
               : new Shadow(Params.string("ship", "ship_0005"), tr.sx * 0.75, 14, -20);
  x0 = tr.x;
  if (pattern == "side") heading = dir > 0 ? 0 : PI;
}

function player(): Entity {
  return World.find("player");
}

function aimAngle(): f32 {
  const p = player();
  if (p.isValid && playerTr.read(p) && playerTr.y > -1000) return angleTo(tr.x, tr.y, playerTr.x, playerTr.y);
  return -PI / 2;
}

function onScreen(): bool {
  return tr.x > -HALF_W + 10 && tr.x < HALF_W - 10 && tr.y < HALF_H - 20 && tr.y > -HALF_H + 60;
}

function fire(): void {
  const bullet = "assets/prefabs/enemy_bullet.prefab.json";
  const a = aimAngle();
  if (fireMode == "aimed") {
    shoot(bullet, tr.x, tr.y - 10, a, 190);
  } else if (fireMode == "straight") {
    shoot(bullet, tr.x, tr.y - 10, -PI / 2, 220);
  } else if (fireMode == "spread3") {
    for (let i: i32 = -1; i <= 1; i++) shoot(bullet, tr.x, tr.y - 10, a + <f32>i * 0.22, 180);
  } else if (fireMode == "spread5") {
    for (let i: i32 = -2; i <= 2; i++) shoot("assets/prefabs/enemy_bullet_big.prefab.json", tr.x, tr.y - 16, -PI / 2 + <f32>i * 0.24, 150);
  } else if (fireMode == "burst") {
    burstLeft = 4;
    burstTimer = 0;
    return;
  }
  sfx("enemy_shoot", 0.35);
}

function move(dt: f32): void {
  let s = speed;
  if (pattern == "sine") {
    tr.x = x0 + <f32>Params.number("amp", 70) * Mathf.sin(t * <f32>Params.number("freq", 2.4));
    tr.y -= s * dt;
    heading = -PI / 2;
  } else if (pattern == "swoop") {
    // Dive in, then curve away toward the other side of the screen.
    if (t > 0.55 && turned < 1.9) {
      const step: f32 = 1.9 * dt;
      heading += dir * step;
      turned += step;
    }
  } else if (pattern == "loop") {
    if (t > 0.8 && turned < PI * 2) {
      const step: f32 = 3.8 * dt;
      heading += dir * step;
      turned += step;
    }
  } else if (pattern == "dive") {
    if (t > 0.5 && t < 1.3) {
      const target = aimAngle();
      let diff = target - heading;
      while (diff > PI) diff -= PI * 2;
      while (diff < -PI) diff += PI * 2;
      heading += Mathf.max(-2.6 * dt, Mathf.min(2.6 * dt, diff));
    }
    if (t > 0.5) s *= 1.7;
  } else if (pattern == "hover") {
    const hoverY = <f32>Params.number("hoverY", 170);
    if (hoverPhase == 0) {
      s = Mathf.max(25, (tr.y - hoverY) * 1.6);
      if (tr.y <= hoverY + 2) hoverPhase = 1;
    } else if (hoverPhase == 1) {
      hoverTime += dt;
      tr.x = tr.x + Mathf.sin(hoverTime * 0.9) * 30 * dt;
      s = 0;
      if (hoverTime > <f32>Params.number("hoverTime", 7)) hoverPhase = 2;
    } else {
      s = 160;  // done hovering: leave through the bottom
    }
  } else if (pattern == "side") {
    if (t > 0.6) heading += dir * -0.35 * dt;  // drift downward
  }
  if (pattern != "sine") {
    tr.x += Mathf.cos(heading) * s * dt;
    tr.y += Mathf.sin(heading) * s * dt;
  }
  tr.rotation = heading - PI / 2;
}

function die(): void {
  dead = true;
  explode(tr.x, tr.y, big);
  sfx(big ? "explode_big" : "explode_small", big ? 0.9 : 0.6);
  if (big) Camera.shake(6, 0.35);
  addScore(Params.number("score", 100));
  GameState.add("stageKills", 1);
  const drop = Params.string("drop", "");
  if (drop.length > 0 && <f32>Math.random() < <f32>Params.number("dropChance", 0)) {
    World.spawn("assets/prefabs/pickup_" + drop + ".prefab.json", tr.x, tr.y);
  }
  removeSelf();
}

function removeSelf(): void {
  const s = shadow;
  if (s !== null) s.destroy();
  Entity.self().destroy();
}

function damage(amount: f32): void {
  if (dead) return;
  hp -= amount;
  flash = 0.07;
  if (hp <= 0) {
    die();
  } else {
    sfx("hit", 0.35);
  }
}

export function onUpdate(dt: f32): void {
  if (dead) return;
  if (!initialized) init();
  t += dt;
  if (!tr.read()) return;

  move(dt);
  tr.write();

  if (onScreen()) seen = true;
  if ((seen && (tr.y < -HALF_H - 60 || tr.y > HALF_H + 120 || Mathf.abs(tr.x) > HALF_W + 80)) || t > 40) {
    removeSelf();
    return;
  }
  const s = shadow;
  if (s !== null) s.follow(tr.x, tr.y, tr.rotation);

  if (fireMode != "none" && onScreen() && GameState.getNumber("stageOver") == 0) {
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
      shoot("assets/prefabs/enemy_bullet.prefab.json", tr.x, tr.y - 18, aimAngle(), 210);
      sfx("enemy_shoot", 0.3);
    }
  }

  if (sprite.read()) {
    if (flash > 0) {
      flash -= dt;
      sprite.setColor(1, 0.45, 0.45, 1);
    } else {
      sprite.setColor(1, 1, 1, 1);
    }
    sprite.write();
  }
}

export function onCollide(index: u32, generation: u32): void {
  if (dead) return;
  const other = new Entity(index, generation);
  if (other.hasTag("player_bullet")) {
    other.destroy();
    GameState.add("stageHits", 1);
    damage(1);
  } else if (other.hasTag("bomb")) {
    if (index != lastBomb) {
      lastBomb = index;
      damage(12);
    }
  } else if (other.hasTag("player")) {
    damage(6);
  }
}
