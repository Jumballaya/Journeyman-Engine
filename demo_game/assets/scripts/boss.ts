// The Flying Fortress. Three phases by remaining health:
//   1 (100-66%)  fans and aimed triplets
//   2 (66-33%)   a rotating spiral; escort fighters join
//   3 (<33%)     faster: rings and aimed bursts, angry tint
import { Camera, Entity, Overrides, World, self, spawn } from "@jm/runtime";
import { HALF_W, PI, DOWN, angleTo, rand, sfx } from "./lib/util";
import { Shadow, explode, fan, shoot } from "./lib/combat";
import { Session } from "./lib/session";

const MAX_HP: f32 = 900;
const HOME_Y: f32 = 170;
const DEATH_SECONDS: f32 = 2.6;

const me = self();
const body = me.transform;
const shadow = new Shadow("ship_0014", 80, 34, -54);

let hp: f32 = MAX_HP;
let t: f32 = 0;
let entering = true;
let attackTimer: f32 = 2.0;
let attackCount: i32 = 0;
let spiralAngle: f32 = 0;
let spiralTimer: f32 = 0;
let escortTimer: f32 = 4;
let hitFlash: f32 = 0;
let hitSoundCooldown: f32 = 0;
let dying: f32 = -1;   // seconds into the death sequence, or -1
let lastPhase: i32 = 1;
let lastBomb: Entity = Entity.NONE;

Session.bossHealth = 1;
Session.bossActive = true;

function phase(): i32 {
  const f = hp / MAX_HP;
  return f > 0.66 ? 1 : f > 0.33 ? 2 : 3;
}

function aimFrom(x: f32, y: f32): f32 {
  const player = World.find("player");
  const p = player.transform;
  return player.isAlive && p.y > -1000 ? angleTo(x, y, p.x, p.y) : DOWN;
}

function attack(): void {
  const p = phase();
  attackCount++;
  if (p == 1 && attackCount % 2 == 0) {
    fan("enemy_bullet_big", body.x, body.y - 50, DOWN, 1.6, 9, 150);
    attackTimer = 1.5;
  } else if (p == 1) {  // aimed triplets from both wing guns
    for (let side: f32 = -1; side <= 1; side += 2) {
      const gx = body.x + side * 60;
      fan("enemy_bullet", gx, body.y - 30, aimFrom(gx, body.y - 30), 0.24, 3, 210);
    }
    attackTimer = 1.1;
  } else if (p == 2) {
    fan("enemy_bullet_big", body.x, body.y - 50, DOWN, 1.2, 7, 170);
    attackTimer = 2.2;
  } else if (attackCount % 2 == 0) {  // ring
    const offset = rand(0, PI);
    for (let i = 0; i < 18; i++) shoot("enemy_bullet_blue", body.x, body.y - 10, offset + PI * 2 * <f32>i / 18, 150);
    attackTimer = 1.0;
  } else {
    fan("enemy_bullet", body.x, body.y - 50, aimFrom(body.x, body.y - 50), 0.36, 5, 250);
    attackTimer = 0.8;
  }
  sfx("enemy_shoot", 0.5);
}

function spiralAndEscorts(p: i32, dt: f32): void {
  spiralTimer -= dt;
  if (spiralTimer <= 0) {
    spiralTimer = p == 3 ? 0.11 : 0.16;
    spiralAngle += 0.42;
    shoot("enemy_bullet_blue", body.x, body.y - 20, spiralAngle, 140);
    shoot("enemy_bullet_blue", body.x, body.y - 20, spiralAngle + PI, 140);
  }
  escortTimer -= dt;
  if (escortTimer <= 0) {
    escortTimer = p == 3 ? 7 : 9;
    for (let side: f32 = -1; side <= 1; side += 2) {
      spawn("enemy_zero", side * 200, 360, new Overrides().paramText("pattern", "dive").param("score", 200));
    }
  }
}

function hit(amount: f32): void {
  if (entering || dying >= 0) return;
  hp -= amount;
  hitFlash = 0.05;
  Session.bossHealth = Mathf.max(0, hp / MAX_HP);
  if (hp <= 0) {
    dying = 0;
    const bullets = World.findAll("enemy_bullet");
    for (let i = 0; i < bullets.length; i++) bullets[i].destroy();
    sfx("explode_big");
  } else if (hitSoundCooldown <= 0) {
    hitSoundCooldown = 0.12;
    sfx("hit", 0.3);
  }
}

// A chain of explosions across the hull while it sinks, then the final blast.
function updateDeath(dt: f32): void {
  dying += dt;
  if (Mathf.floor(dying * 9) != Mathf.floor((dying - dt) * 9)) {
    explode(body.x + rand(-80, 80), body.y + rand(-40, 50), Math.random() < 0.4);
    sfx("explode_small", 0.7);
    Camera.shake(8, 0.25);
  }
  body.y -= 22 * dt;
  body.rotation = PI + Mathf.sin(dying * 7) * 0.05;
  shadow.follow(body);
  if (dying < DEATH_SECONDS) return;

  for (let i = 0; i < 6; i++) explode(body.x + rand(-70, 70), body.y + rand(-40, 40), true);
  sfx("explode_big");
  Camera.shake(18, 1.0);
  Session.flash(1.0);
  Session.addScore(50000);
  Session.countKill();
  Session.bossActive = false;
  Session.bossDefeated = true;
  shadow.destroy();
  me.destroy();
}

export function onUpdate(dt: f32): void {
  t += dt;
  hitSoundCooldown -= dt;
  if (dying >= 0) {
    updateDeath(dt);
    return;
  }

  if (entering) {
    body.y -= Mathf.max(30, (body.y - HOME_Y) * 0.9) * dt;
    if (body.y <= HOME_Y + 1) {
      entering = false;
      t = 0;
    }
  } else {
    const p = phase();
    if (p != lastPhase) {
      lastPhase = p;
      Camera.shake(6, 0.5);
      Session.flash(0.5);
      sfx("warning", 0.5);
      attackTimer = 1.2;
    }
    body.x = Mathf.sin(t * (p == 3 ? 1.0 : 0.55)) * (HALF_W - 110);
    body.y = HOME_Y + Mathf.sin(t * 1.3) * 14;
    attackTimer -= dt;
    if (attackTimer <= 0) attack();
    if (p >= 2) spiralAndEscorts(p, dt);
  }
  shadow.follow(body);

  hitFlash -= dt;
  const angry: f32 = phase() == 3 ? 0.75 + 0.25 * Mathf.sin(t * 8) : 1.0;
  if (hitFlash > 0) me.sprite.setColor(1, 0.6, 0.6);
  else me.sprite.setColor(1, angry, angry);
}

export function onCollide(other: Entity): void {
  if (other.hasTag("player_bullet")) {
    other.destroy();
    Session.countHit();
    hit(1);
  } else if (other.hasTag("bomb") && !other.equals(lastBomb)) {
    lastBomb = other;
    hit(60);
  }
}
