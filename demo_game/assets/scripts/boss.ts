// The Flying Fortress. Three phases by remaining health:
//   1 (100-66%)  fans + aimed triplets
//   2 (66-33%)   rotating spiral, escort fighters join
//   3 (<33%)     faster, rings + aimed bursts, angry tint
// Health is published to GameState (bossHp / bossHpMax) for the HUD.
import { Entity, World, GameState, Camera, TransformComponent, SpriteComponent } from "@jm/runtime";
import { HALF_W, PI, rand, sfx, shoot, angleTo, explode, addScore, Shadow } from "./lib/game";

const MAX_HP: f32 = 420;
const HOME_Y: f32 = 170;

const tr = new TransformComponent();
const sprite = new SpriteComponent();
const playerTr = new TransformComponent();

let hp: f32 = MAX_HP;
let t: f32 = 0;
let entering = true;
let attackTimer: f32 = 2.0;
let attackStep: i32 = 0;
let spiralAngle: f32 = 0;
let spiralTimer: f32 = 0;
let escortTimer: f32 = 4;
let flash: f32 = 0;
let dying: f32 = -1;   // >= 0 during the death sequence
let lastBomb: u32 = 0xFFFFFFFF;
let lastPhase: i32 = 1;
let started = false;
let shadow: Shadow | null = null;
let hitSoundCooldown: f32 = 0;

function phase(): i32 {
  const f = hp / MAX_HP;
  return f > 0.66 ? 1 : f > 0.33 ? 2 : 3;
}

function aim(fromX: f32, fromY: f32): f32 {
  const p = World.find("player");
  if (p.isValid && playerTr.read(p) && playerTr.y > -1000) return angleTo(fromX, fromY, playerTr.x, playerTr.y);
  return -PI / 2;
}

function fan(count: i32, spread: f32, speed: f32, prefab: string): void {
  for (let i = 0; i < count; i++) {
    const a = -PI / 2 + spread * (<f32>i / <f32>(count - 1) - 0.5);
    shoot(prefab, tr.x, tr.y - 50, a, speed);
  }
}

function ring(count: i32, speed: f32, offset: f32): void {
  for (let i = 0; i < count; i++) {
    shoot("assets/prefabs/enemy_bullet_blue.prefab.json", tr.x, tr.y - 10,
          offset + PI * 2 * <f32>i / <f32>count, speed);
  }
}

function attack(): void {
  const p = phase();
  const big = "assets/prefabs/enemy_bullet_big.prefab.json";
  const small = "assets/prefabs/enemy_bullet.prefab.json";
  attackStep++;
  if (p == 1) {
    if (attackStep % 2 == 0) {
      fan(9, 1.6, 150, big);
      attackTimer = 1.5;
    } else {
      for (let k = -1; k <= 1; k += 2) {
        const gx = tr.x + <f32>k * 60;
        const a = aim(gx, tr.y - 30);
        for (let i = -1; i <= 1; i++) shoot(small, gx, tr.y - 30, a + <f32>i * 0.12, 210);
      }
      attackTimer = 1.1;
    }
  } else if (p == 2) {
    fan(7, 1.2, 170, big);
    attackTimer = 2.2;
  } else {
    if (attackStep % 2 == 0) {
      ring(18, 150, rand(0, PI));
      attackTimer = 1.0;
    } else {
      const a = aim(tr.x, tr.y - 50);
      for (let i = -2; i <= 2; i++) shoot(small, tr.x, tr.y - 50, a + <f32>i * 0.09, 250);
      attackTimer = 0.8;
    }
  }
  sfx("enemy_shoot", 0.5);
}

function hit(amount: f32): void {
  if (entering || dying >= 0) return;
  hp -= amount;
  flash = 0.05;
  GameState.setNumber("bossHp", Mathf.max(0, hp));
  if (hp <= 0) {
    dying = 0;
    GameState.setNumber("bossDying", 1);
    const bullets = World.findAll("enemy_bullet");
    for (let i = 0; i < bullets.length; i++) bullets[i].destroy();
    sfx("explode_big", 1.0);
  } else if (hitSoundCooldown <= 0) {
    hitSoundCooldown = 0.12;
    sfx("hit", 0.3);
  }
}

export function onUpdate(dt: f32): void {
  t += dt;
  hitSoundCooldown -= dt;
  if (!tr.read()) return;

  if (!started) {
    started = true;
    shadow = new Shadow("ship_0014", 80, 34, -54);
    GameState.setNumber("bossHp", MAX_HP);
    GameState.setNumber("bossHpMax", MAX_HP);
    GameState.setNumber("bossActive", 1);
  }

  if (dying >= 0) {
    dying += dt;
    // A chain of explosions across the hull, then the final blast.
    if (Mathf.floor(dying * 9) != Mathf.floor((dying - dt) * 9)) {
      explode(tr.x + rand(-80, 80), tr.y + rand(-40, 50), Math.random() < 0.4);
      sfx("explode_small", 0.7);
      Camera.shake(8, 0.25);
    }
    tr.y -= 22 * dt;
    tr.rotation = PI + Mathf.sin(dying * 7) * 0.05;
    tr.write();
    const dyingShadow = shadow;
    if (dyingShadow !== null) dyingShadow.follow(tr.x, tr.y, tr.rotation);
    if (dying > 2.6) {
      for (let i = 0; i < 6; i++) explode(tr.x + rand(-70, 70), tr.y + rand(-40, 40), true);
      sfx("explode_big", 1.0);
      Camera.shake(18, 1.0);
      GameState.setNumber("flash", 1.0);
      addScore(50000);
      GameState.add("stageKills", 1);
      GameState.setNumber("bossActive", 0);
      GameState.setNumber("bossDefeated", 1);
      const sh = shadow;
      if (sh !== null) sh.destroy();
      Entity.self().destroy();
    }
    return;
  }

  if (entering) {
    tr.y -= Mathf.max(30, (tr.y - HOME_Y) * 0.9) * dt;
    if (tr.y <= HOME_Y + 1) {
      entering = false;
      t = 0;
    }
  } else {
    const p = phase();
    if (p != lastPhase) {
      lastPhase = p;
      Camera.shake(6, 0.5);
      GameState.setNumber("flash", 0.5);
      sfx("warning", 0.5);
      attackTimer = 1.2;
    }
    const sway: f32 = p == 3 ? 1.0 : 0.55;
    tr.x = Mathf.sin(t * sway) * (HALF_W - 110);
    tr.y = HOME_Y + Mathf.sin(t * 1.3) * 14;

    attackTimer -= dt;
    if (attackTimer <= 0) attack();

    if (p >= 2) {
      spiralTimer -= dt;
      if (spiralTimer <= 0) {
        spiralTimer = p == 3 ? 0.11 : 0.16;
        spiralAngle += 0.42;
        shoot("assets/prefabs/enemy_bullet_blue.prefab.json", tr.x, tr.y - 20, spiralAngle, 140);
        shoot("assets/prefabs/enemy_bullet_blue.prefab.json", tr.x, tr.y - 20, spiralAngle + PI, 140);
      }
      escortTimer -= dt;
      if (escortTimer <= 0) {
        escortTimer = p == 3 ? 7 : 9;
        for (let k = -1; k <= 1; k += 2) {
          World.spawn("assets/prefabs/enemy_zero.prefab.json", <f32>k * 200, 360,
                      '{"ScriptComponent":{"params":{"pattern":"dive","score":200}}}');
        }
      }
    }
  }
  tr.write();
  const sh = shadow;
  if (sh !== null) sh.follow(tr.x, tr.y, tr.rotation);

  if (sprite.read()) {
    const angry: f32 = phase() == 3 ? 0.75 + 0.25 * Mathf.sin(t * 8) : 1.0;
    if (flash > 0) {
      flash -= dt;
      sprite.setColor(1, 0.6, 0.6, 1);
    } else {
      sprite.setColor(1, angry, angry, 1);
    }
    sprite.write();
  }
}

export function onCollide(index: u32, generation: u32): void {
  const other = new Entity(index, generation);
  if (other.hasTag("player_bullet")) {
    other.destroy();
    GameState.add("stageHits", 1);
    hit(1);
  } else if (other.hasTag("bomb")) {
    if (index != lastBomb) {
      lastBomb = index;
      hit(25);
    }
  }
}
