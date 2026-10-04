// The player's fighter: movement, guns, bombs, pickups, death and respawn.
import { Camera, Entity, Input, World, self, spawn } from "@jm/runtime";
import { HALF_W, HALF_H, UP, approach, clamp, sfx } from "./lib/util";
import { Shadow, explode, shoot } from "./lib/combat";
import { Session } from "./lib/session";

const SPEED: f32 = 270;
const HOME_Y: f32 = -220;
const FIRE_INTERVAL: f32 = 0.09;
const BULLET_SPEED: f32 = 780;
const RESPAWN_DELAY: f32 = 1.6;
const SPAWN_SHIELD: f32 = 2.5;
const OFFSCREEN_Y: f32 = -2000;  // parked here while dead, out of every collision

const me = self();
const body = me.transform;
const shadow = new Shadow("ship_0000", 24, 16, -24);

let t: f32 = 0;
let fireCooldown: f32 = 0;
let shield: f32 = 0;      // invulnerable while > 0
let flyIn: f32 = 0;       // autopilot onto the screen while > 0
let deadFor: f32 = -1;    // >= 0 while waiting to respawn

function enter(): void {
  body.setPosition(0, -HALF_H - 60);
  flyIn = 1.0;
  shield = SPAWN_SHIELD;
}
enter();

function fire(): void {
  const x = body.x;
  const y = body.y + 26;
  shoot("player_bullet", x - 9, y, UP, BULLET_SPEED);
  shoot("player_bullet", x + 9, y, UP, BULLET_SPEED);
  let shots = 2;
  if (Session.power >= 2) {
    shoot("player_bullet", x - 16, y - 6, UP + 0.14, BULLET_SPEED, true);
    shoot("player_bullet", x + 16, y - 6, UP - 0.14, BULLET_SPEED, true);
    shots += 2;
  }
  if (Session.power >= 3) {
    shoot("player_bullet", x, y + 4, UP, BULLET_SPEED);
    shoot("player_bullet", x - 20, y - 10, UP + 0.3, BULLET_SPEED, true);
    shoot("player_bullet", x + 20, y - 10, UP - 0.3, BULLET_SPEED, true);
    shots += 3;
  }
  spawn("muzzle", x, y + 6);
  Session.countShots(shots);
  sfx("shoot", 0.22);
}

// Clears every enemy bullet and damages everything on screen (bomb_blast).
function bomb(): void {
  Session.bombs--;
  spawn("bomb_blast", 0, 0);
  const bullets = World.findAll("enemy_bullet");
  for (let i = 0; i < bullets.length; i++) bullets[i].destroy();
  Session.flash(1.0);
  Camera.shake(10, 0.8);
  shield = Mathf.max(shield, 1.0);
  sfx("bomb");
}

function die(): void {
  explode(body.x, body.y, true);
  sfx("player_die");
  Camera.shake(14, 0.6);
  Session.flash(0.6);
  Session.countDeath();
  Session.power--;
  Session.bombs = max(Session.bombs, 2);  // a new ship always has two bombs
  Session.lives--;
  if (Session.lives < 0) Session.gameOver = true;
  body.y = OFFSCREEN_Y;
  deadFor = 0;
}

function steer(dt: f32): void {
  if (flyIn > 0) {
    flyIn -= dt;
    body.y = approach(body.y, HOME_Y, dt * 4);
    return;
  }
  let dx = Input.axis("left", "right");
  let dy = Input.axis("down", "up");
  const len = Mathf.sqrt(dx * dx + dy * dy);
  if (len > 1) {  // diagonals aren't faster
    dx /= len;
    dy /= len;
  }
  body.x = clamp(body.x + dx * SPEED * dt, -HALF_W + 22, HALF_W - 22);
  body.y = clamp(body.y + dy * SPEED * dt, -HALF_H + 30, HALF_H - 70);
  body.rotation = approach(body.rotation, -dx * 0.12, dt * 10);  // bank into turns
}

export function onUpdate(dt: f32): void {
  t += dt;
  shadow.follow(body);

  if (deadFor >= 0) {
    deadFor += dt;
    if (deadFor >= RESPAWN_DELAY && !Session.gameOver) {
      deadFor = -1;
      enter();
    }
    return;
  }
  if (Session.stageOver) {  // fly off the top of the screen
    body.y += 420 * dt;
    return;
  }

  steer(dt);
  if (shield > 0) shield -= dt;
  me.sprite.alpha = shield > 0 && <i32>Mathf.floor(t * 14) % 2 == 0 ? 0.35 : 1.0;

  fireCooldown -= dt;
  if (flyIn <= 0.6 && Input.down("fire") && fireCooldown <= 0) {
    fireCooldown = FIRE_INTERVAL;
    fire();
  }
  if (flyIn <= 0 && Input.pressed("bomb") && Session.bombs > 0) bomb();
}

function collect(pickup: Entity): void {
  if (pickup.hasTag("pickup_power")) {
    if (Session.power < 3) {
      Session.power++;
      sfx("powerup", 0.9);
    } else {
      Session.addScore(2000);
      sfx("pickup", 0.8);
    }
  } else if (pickup.hasTag("pickup_life")) {
    Session.lives++;
    sfx("extra_life", 0.9);
  } else if (pickup.hasTag("pickup_bomb")) {
    Session.bombs++;
    sfx("pickup", 0.9);
  }
  Session.addScore(500);
  pickup.destroy();
}

export function onCollide(other: Entity): void {
  if (deadFor >= 0) return;
  if (other.hasTag("pickup")) {
    collect(other);
    return;
  }
  if (shield > 0 || flyIn > 0 || Session.stageOver) return;
  if (other.hasTag("enemy_bullet")) {
    other.destroy();
    die();
  } else if (other.hasTag("enemy")) {
    die();
  }
}
