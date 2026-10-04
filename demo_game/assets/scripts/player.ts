// The player's fighter: movement, firing, bombs, damage, pickups, respawn.
import {
  Entity, World, GameState, Input, Time, Camera, TransformComponent, SpriteComponent,
} from "@jm/runtime";
import { HALF_W, HALF_H, PI, clamp, approach, sfx, shoot, explode, addScore } from "./lib/game";

const SPEED: f32 = 270;
const HOME_Y: f32 = -220;
const FIRE_INTERVAL: f32 = 0.09;
const RESPAWN_DELAY: f32 = 1.6;
const SPAWN_SHIELD: f32 = 2.5;

const tr = new TransformComponent();
const sprite = new SpriteComponent();

let t: f32 = 0;
let fireCooldown: f32 = 0;
let shield: f32 = 0;      // invulnerable while > 0
let flyIn: f32 = 0;       // auto-pilot onto the screen while > 0
let deadFor: f32 = -1;    // >= 0 while waiting to respawn
let started = false;

function power(): i32 { return <i32>GameState.getNumber("power", 1); }

function enter(): void {
  tr.read();
  tr.x = 0;
  tr.y = -HALF_H - 60;
  tr.write();
  flyIn = 1.0;
  shield = SPAWN_SHIELD;
}

function fire(): void {
  const p = power();
  const x = tr.x;
  const y = tr.y + 26;
  const up = PI / 2;
  const bullet = "assets/prefabs/player_bullet.prefab.json";
  const speed: f32 = 780;
  let shots = 2;
  shoot(bullet, x - 9, y, up, speed);
  shoot(bullet, x + 9, y, up, speed);
  if (p >= 2) {
    shoot(bullet, x - 16, y - 6, up + 0.14, speed, true);
    shoot(bullet, x + 16, y - 6, up - 0.14, speed, true);
    shots += 2;
  }
  if (p >= 3) {
    shoot(bullet, x, y + 4, up, speed);
    shoot(bullet, x - 20, y - 10, up + 0.3, speed, true);
    shoot(bullet, x + 20, y - 10, up - 0.3, speed, true);
    shots += 3;
  }
  World.spawn("assets/prefabs/muzzle.prefab.json", x, y + 6);
  GameState.add("stageShots", shots);
  sfx("shoot", 0.22);
}

function bomb(): void {
  GameState.add("bombs", -1);
  World.spawn("assets/prefabs/bomb_blast.prefab.json", 0, 0);
  const bullets = World.findAll("enemy_bullet");
  for (let i = 0; i < bullets.length; i++) bullets[i].destroy();
  GameState.setNumber("flash", 1.0);
  Camera.shake(10, 0.8);
  shield = Mathf.max(shield, 1.0);
  sfx("bomb", 1.0);
}

function die(): void {
  explode(tr.x, tr.y, true);
  sfx("player_die", 1.0);
  Camera.shake(14, 0.6);
  GameState.setNumber("flash", 0.6);
  GameState.add("stageDeaths", 1);
  GameState.setNumber("power", Mathf.max(1, <f32>(power() - 1)));
  // Bombs refill to at least 2 on a new ship, like the arcade games.
  GameState.setNumber("bombs", Math.max(2, GameState.getNumber("bombs")));
  const lives = GameState.add("lives", -1);
  tr.y = -2000;  // park off-screen so nothing else collides
  tr.write();
  deadFor = 0;
  if (lives < 0) GameState.setNumber("gameOver", 1);
}

export function onUpdate(dt: f32): void {
  t += dt;
  if (!started) {
    started = true;
    enter();
  }
  if (!tr.read()) return;

  // Stage cleared: fly up and off the screen.
  if (GameState.getNumber("stageOver") > 0 && deadFor < 0) {
    tr.y += 420 * dt;
    tr.write();
    return;
  }

  if (deadFor >= 0) {
    deadFor += dt;
    if (deadFor >= RESPAWN_DELAY && GameState.getNumber("lives") >= 0 && GameState.getNumber("gameOver") == 0) {
      deadFor = -1;
      enter();
    }
    return;
  }

  if (flyIn > 0) {
    flyIn -= dt;
    tr.y = approach(tr.y, HOME_Y, dt * 4);
  } else {
    let dx = Input.axis("left", "right");
    let dy = Input.axis("down", "up");
    const len = Mathf.sqrt(dx * dx + dy * dy);
    if (len > 1) {
      dx /= len;
      dy /= len;
    }
    tr.x = clamp(tr.x + dx * SPEED * dt, -HALF_W + 22, HALF_W - 22);
    tr.y = clamp(tr.y + dy * SPEED * dt, -HALF_H + 30, HALF_H - 70);
    // Slight bank: lean toward the direction of travel.
    tr.rotation = approach(tr.rotation, -dx * 0.12, dt * 10);
  }
  tr.write();

  if (shield > 0) shield -= dt;
  if (sprite.read()) {
    sprite.a = shield > 0 && Mathf.floor(t * 14) % 2 == 0 ? 0.35 : 1.0;
    sprite.write();
  }

  fireCooldown -= dt;
  if (flyIn <= 0.6 && Input.down("fire") && fireCooldown <= 0) {
    fireCooldown = FIRE_INTERVAL;
    fire();
  }
  if (flyIn <= 0 && Input.pressed("bomb") && GameState.getNumber("bombs") > 0) {
    bomb();
  }
}

export function onCollide(index: u32, generation: u32): void {
  const other = new Entity(index, generation);
  if (deadFor >= 0) return;

  if (other.hasTag("pickup")) {
    if (other.hasTag("pickup_power")) {
      if (power() < 3) {
        GameState.add("power", 1);
        sfx("powerup", 0.9);
      } else {
        addScore(2000);
        sfx("pickup", 0.8);
      }
    } else if (other.hasTag("pickup_life")) {
      GameState.setNumber("lives", Math.min(5, GameState.getNumber("lives") + 1));
      sfx("extra_life", 0.9);
    } else if (other.hasTag("pickup_bomb")) {
      GameState.setNumber("bombs", Math.min(5, GameState.getNumber("bombs") + 1));
      sfx("pickup", 0.9);
    }
    addScore(500);
    other.destroy();
    return;
  }

  if (shield > 0 || flyIn > 0 || GameState.getNumber("stageOver") > 0) return;
  if (other.hasTag("enemy_bullet")) {
    other.destroy();
    die();
  } else if (other.hasTag("enemy")) {
    die();
  }
}
