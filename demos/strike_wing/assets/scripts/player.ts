// The player's fighter: movement, guns, bombs, pickups, death and respawn.
import { Audio, Projectile, Timer, Vec2, Rect, blink, lerp, Camera, Entity, Input, World, self, spawn } from "@jm/runtime";
import { HALF_W, HALF_H, UP } from "./lib/util";
import { explode } from "./lib/combat";
import * as Session from "./lib/session";

const SPEED: f32 = 270;
const HOME_Y: f32 = -220;
const FIRE_INTERVAL: f32 = 0.09;
const bullet = new Projectile("player_bullet", 780);
const angledBullet = new Projectile("player_bullet", 780, true);
const movement = new Vec2();
const playArea = new Rect(-HALF_W + 22, -HALF_H + 30, HALF_W - 22, HALF_H - 70);
const RESPAWN_DELAY: f32 = 1.6;
const SPAWN_SHIELD: f32 = 2.5;
const OFFSCREEN_Y: f32 = -2000;  // parked here while dead, out of every collision

const me = self();
const body = me.transform;
me.sprite.shadow({ x: 16, y: -24, scale: 0.75, layer: 2, r: 0.02, g: 0.05, b: 0.12, alpha: 0.32 });

let t: f32 = 0;
const fireCooldown = new Timer();
const shield = new Timer();      // invulnerable until ready
const flyIn = new Timer();       // autopilot until ready
const respawn = new Timer();
let dead = false;

function enter(): void {
  body.setPosition(0, -HALF_H - 60);
  flyIn.start(1);
  shield.start(SPAWN_SHIELD);
}
enter();

function fire(): void {
  const x = body.x;
  const y = body.y + 26;
  bullet.fire(x - 9, y, UP);
  bullet.fire(x + 9, y, UP);
  let shots = 2;
  if (Session.power.value >= 2) {
    angledBullet.fire(x - 16, y - 6, UP + 0.14);
    angledBullet.fire(x + 16, y - 6, UP - 0.14);
    shots += 2;
  }
  if (Session.power.value >= 3) {
    bullet.fire(x, y + 4, UP);
    angledBullet.fire(x - 20, y - 10, UP + 0.3);
    angledBullet.fire(x + 20, y - 10, UP - 0.3);
    shots += 3;
  }
  spawn("muzzle", x, y + 6);
  Session.shots.add(shots);
  Audio.play("shoot", 0.22);
}

// Clears every enemy bullet and damages everything on screen (bomb_blast).
function bomb(): void {
  Session.bombs.value--;
  spawn("bomb_blast", 0, 0);
  World.destroyAll("enemy_bullet");
  Session.flash.record(1.0);
  Camera.shake(20, 0.8);
  shield.extend(1);
  Audio.play("bomb");
}

function die(): void {
  explode(body.x, body.y, true);
  Audio.play("player_die");
  Camera.shake(28, 0.6);
  Session.flash.record(0.6);
  Session.deaths.add(1);
  Session.power.value--;
  Session.bombs.value = max(Session.bombs.value, 2);  // a new ship always has two bombs
  Session.lives.value--;
  if (Session.lives.value < 0) Session.gameOver.value = true;
  body.y = OFFSCREEN_Y;
  dead = true;
  respawn.start(RESPAWN_DELAY);
}

function steer(dt: f32): void {
  if (!flyIn.ready) {
    flyIn.tick(dt);
    body.y = lerp(body.y, HOME_Y, dt * 4);
    return;
  }
  Input.vector("left", "right", "down", "up", movement);
  body.x = playArea.clampX(body.x + movement.x * SPEED * dt);
  body.y = playArea.clampY(body.y + movement.y * SPEED * dt);
  body.rotation = lerp(body.rotation, -movement.x * 0.12, dt * 10); // bank into turns
}

export function onUpdate(dt: f32): void {
  t += dt;

  if (dead) {
    respawn.tick(dt);
    if (respawn.ready && !Session.gameOver.value) {
      dead = false;
      enter();
    }
    return;
  }
  if (Session.stageOver.value) {  // fly off the top of the screen
    body.y += 420 * dt;
    return;
  }

  steer(dt);
  shield.tick(dt);
  me.sprite.alpha = shield.ready ? 1 : blink(t, 14, 0.35, 1);

  fireCooldown.tick(dt);
  if (flyIn.remaining <= 0.6 && Input.down("fire") && fireCooldown.ready) {
    fireCooldown.start(FIRE_INTERVAL);
    fire();
  }
  if (flyIn.ready && Input.pressed("bomb") && Session.bombs.value > 0) bomb();
}

function collect(pickup: Entity): void {
  if (pickup.hasTag("pickup_power")) {
    if (Session.power.value < 3) {
      Session.power.value++;
      Audio.play("powerup", 0.9);
    } else {
      Session.score.add(2000);
      Audio.play("pickup", 0.8);
    }
  } else if (pickup.hasTag("pickup_life")) {
    Session.lives.value++;
    Audio.play("extra_life", 0.9);
  } else if (pickup.hasTag("pickup_bomb")) {
    Session.bombs.value++;
    Audio.play("pickup", 0.9);
  }
  Session.score.add(500);
  pickup.destroy();
}

export function onCollide(other: Entity): void {
  if (dead) return;
  if (other.hasTag("pickup")) {
    collect(other);
    return;
  }
  if (!shield.ready || !flyIn.ready || Session.stageOver.value) return;
  if (other.hasTag("enemy_bullet")) {
    other.destroy();
    die();
  } else if (other.hasTag("enemy")) {
    die();
  }
}
