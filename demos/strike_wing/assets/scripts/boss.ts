// The Flying Fortress. Three phases by remaining health:
//   1 (100-66%)  fans and aimed triplets
//   2 (66-33%)   a rotating spiral; escort fighters join
//   3 (<33%)     faster: rings and aimed bursts, angry tint
import {
  Projectile, Timer, Interval, Health, HitHistory, PI, Random, angleTo, Camera, Entity, Overrides,
  World, self, spawn,
} from "@jm/runtime";
import { HALF_W, DOWN } from "./lib/util";
import { Shadow, explode } from "./lib/combat";
import * as Session from "./lib/session";

const MAX_HP: f32 = 900;
const HOME_Y: f32 = 170;
const DEATH_SECONDS: f32 = 2.6;

const me = self();
const body = me.transform;
const shadow = new Shadow("ship_0014", 80, 34, -54);

const health = new Health(MAX_HP);
let t: f32 = 0;
let entering = true;
const attackTimer = new Timer(2.0);
let attackCount: i32 = 0;
let spiralAngle: f32 = 0;
const spiralTimer = new Timer();
const escortTimer = new Timer(4);
const hitFlash = new Timer();
const hitSoundCooldown = new Timer();
let dying: f32 = -1;   // seconds into the death sequence, or -1
let lastPhase: i32 = 1;
const bombHits = new HitHistory();
const deathExplosions = new Interval(1.0 / 9.0);

Session.bossHealth.value = 1;
Session.bossActive.value = true;

function phase(): i32 {
  const f = health.fraction;
  return f > 0.66 ? 1 : f > 0.33 ? 2 : 3;
}

function aimFrom(x: f32, y: f32): f32 {
  const player = World.find("player");
  const p = player.transform;
  return player.isAlive && p.y > -1000 ? angleTo(x, y, p.x, p.y) : DOWN;
}

const heavyShot = new Projectile("enemy_bullet_big", 150);
const fastHeavyShot = new Projectile("enemy_bullet_big", 170);
const aimedShot = new Projectile("enemy_bullet", 210);
const fastShot = new Projectile("enemy_bullet", 250);
const ringShot = new Projectile("enemy_bullet_blue", 150);
const spiralShot = new Projectile("enemy_bullet_blue", 140);

function attack(): void {
  const p = phase();
  attackCount++;
  if (p == 1 && attackCount % 2 == 0) {
    heavyShot.fan(body.x, body.y - 50, DOWN, 9, 1.6);
    attackTimer.start(1.5);
  } else if (p == 1) {  // aimed triplets from both wing guns
    for (let side: f32 = -1; side <= 1; side += 2) {
      const gx = body.x + side * 60;
      aimedShot.fan(gx, body.y - 30, aimFrom(gx, body.y - 30), 3, 0.24);
    }
    attackTimer.start(1.1);
  } else if (p == 2) {
    fastHeavyShot.fan(body.x, body.y - 50, DOWN, 7, 1.2);
    attackTimer.start(2.2);
  } else if (attackCount % 2 == 0) {  // ring
    ringShot.ring(body.x, body.y - 10, 18, Random.range(0, PI));
    attackTimer.start(1.0);
  } else {
    fastShot.fan(body.x, body.y - 50, aimFrom(body.x, body.y - 50), 5, 0.36);
    attackTimer.start(0.8);
  }
  Audio.play("enemy_shoot", 0.5);
}

function spiralAndEscorts(p: i32, dt: f32): void {
  spiralTimer.tick(dt);
  if (spiralTimer.ready) {
    spiralTimer.start(p == 3 ? 0.11 : 0.16);
    spiralAngle += 0.42;
    spiralShot.fire(body.x, body.y - 20, spiralAngle);
    spiralShot.fire(body.x, body.y - 20, spiralAngle + PI);
  }
  escortTimer.tick(dt);
  if (escortTimer.ready) {
    escortTimer.start(p == 3 ? 7 : 9);
    for (let side: f32 = -1; side <= 1; side += 2) {
      spawn("enemy_zero", side * 200, 360, new Overrides().paramText("pattern", "dive").param("score", 200));
    }
  }
}

function hit(amount: f32): void {
  if (entering || dying >= 0) return;
  const killed = health.damage(amount);
  hitFlash.start(0.05);
  Session.bossHealth.value = health.fraction;
  if (killed) {
    dying = 0;
    World.destroyAll("enemy_bullet");
    Audio.play("explode_big");
  } else if (hitSoundCooldown.ready) {
    hitSoundCooldown.start(0.12);
    Audio.play("hit", 0.3);
  }
}

// A chain of explosions across the hull while it sinks, then the final blast.
function updateDeath(dt: f32): void {
  dying += dt;
  if (deathExplosions.tick(dt) > 0) {
    explode(body.x + Random.range(-80, 80), body.y + Random.range(-40, 50), Random.chance(0.4));
    Audio.play("explode_small", 0.7);
    Camera.shake(8, 0.25);
  }
  body.y -= 22 * dt;
  body.rotation = PI + Mathf.sin(dying * 7) * 0.05;
  shadow.follow(body);
  if (dying < DEATH_SECONDS) return;

  for (let i = 0; i < 6; i++) explode(body.x + Random.range(-70, 70), body.y + Random.range(-40, 40), true);
  Audio.play("explode_big");
  Camera.shake(18, 1.0);
  Session.flash.record(1.0);
  Session.score.add(50000);
  Session.kills.add(1);
  Session.bossActive.value = false;
  Session.bossDefeated.value = true;
  shadow.destroy();
  me.destroy();
}

export function onUpdate(dt: f32): void {
  t += dt;
  hitSoundCooldown.tick(dt);
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
      Session.flash.record(0.5);
      Audio.play("warning", 0.5);
      attackTimer.start(1.2);
    }
    body.x = Mathf.sin(t * (p == 3 ? 1.0 : 0.55)) * (HALF_W - 110);
    body.y = HOME_Y + Mathf.sin(t * 1.3) * 14;
    attackTimer.tick(dt);
    if (attackTimer.ready) attack();
    if (p >= 2) spiralAndEscorts(p, dt);
  }
  shadow.follow(body);

  hitFlash.tick(dt);
  const angry: f32 = phase() == 3 ? 0.75 + 0.25 * Mathf.sin(t * 8) : 1.0;
  if (!hitFlash.ready) me.sprite.setColor(1, 0.6, 0.6);
  else me.sprite.setColor(1, angry, angry);
}

export function onCollide(other: Entity): void {
  if (other.hasTag("player_bullet")) {
    other.destroy();
    Session.hits.add(1);
    hit(1);
  } else if (other.hasTag("bomb") && bombHits.accept(other)) {
    hit(60);
  }
}
