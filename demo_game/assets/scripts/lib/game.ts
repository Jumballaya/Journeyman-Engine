// Shared gameplay helpers for Strike Wing. Not a script itself (it has no
// onUpdate and isn't listed in the manifest); scripts import from it.
import {
  Entity, World, GameState, Save, Sound, Bus, Audio, Input, Window, UI, PostEffect, Scene, TransformComponent,
} from "@jm/runtime";

// World space is the 480x640 logical screen, origin at the center, y up.
export const HALF_W: f32 = 240;
export const HALF_H: f32 = 320;
export const PI: f32 = Mathf.PI;

export const SCENE_TITLE = "scenes/title.scene.json";
export const SCENE_CLEAR = "scenes/stage_clear.scene.json";
export const SCENE_GAME_OVER = "scenes/game_over.scene.json";
export const SCENE_VICTORY = "scenes/victory.scene.json";
export const SHADER_WIPE = "assets/shaders/wipe.frag";
export const SHADER_DISSOLVE = "assets/shaders/dissolve.frag";

export function stageScene(stage: i32): string {
  if (stage == 1) return "scenes/level1.scene.json";
  if (stage == 2) return "scenes/level2.scene.json";
  return "scenes/boss.scene.json";
}

export function stageName(stage: i32): string {
  if (stage == 1) return "PACIFIC DAWN";
  if (stage == 2) return "JUNGLE FRONT";
  return "IRON FORTRESS";
}

// ---- Math -----------------------------------------------------------------

export function rand(a: f32, b: f32): f32 {
  return a + <f32>Math.random() * (b - a);
}

export function randInt(a: i32, b: i32): i32 {  // inclusive
  return a + <i32>Math.floor(Math.random() * <f64>(b - a + 1));
}

export function clamp(v: f32, lo: f32, hi: f32): f32 {
  return v < lo ? lo : v > hi ? hi : v;
}

export function approach(v: f32, target: f32, rate: f32): f32 {
  return v + (target - v) * clamp(rate, 0, 1);
}

// Zero-padded score text ("0012340").
export function pad(n: f64, digits: i32 = 7): string {
  const s = (<i64>Math.max(0, Math.floor(n))).toString();
  return s.length >= digits ? s : "0".repeat(digits - s.length) + s;
}

// ---- Session & save state ----------------------------------------------------

export function newGame(): void {
  GameState.clear();
  GameState.setNumber("score", 0);
  GameState.setNumber("lives", 2);   // reserve lives (3 ships total)
  GameState.setNumber("bombs", 3);
  GameState.setNumber("power", 1);
  GameState.setNumber("stage", 1);
}

// Called by the director when a stage starts: snapshot for "restart stage"
// and reset per-stage statistics.
export function beginStage(stage: i32): void {
  GameState.setNumber("stage", stage);
  if (GameState.getNumber("snapshotStage") != <f64>stage) {
    GameState.setNumber("snapshotStage", stage);
    GameState.setNumber("snap.score", GameState.getNumber("score"));
    GameState.setNumber("snap.lives", GameState.getNumber("lives"));
    GameState.setNumber("snap.bombs", GameState.getNumber("bombs"));
    GameState.setNumber("snap.power", GameState.getNumber("power"));
  }
  GameState.setNumber("stageKills", 0);
  GameState.setNumber("stageShots", 0);
  GameState.setNumber("stageHits", 0);
  GameState.setNumber("stageDeaths", 0);
  GameState.setNumber("stageOver", 0);
  GameState.setNumber("gameOver", 0);
  GameState.setNumber("bossActive", 0);
  GameState.setNumber("bossDying", 0);
  GameState.setNumber("bossDefeated", 0);
  GameState.setNumber("flash", 0);
}

export function restoreStageSnapshot(): void {
  GameState.setNumber("score", GameState.getNumber("snap.score"));
  GameState.setNumber("lives", GameState.getNumber("snap.lives"));
  GameState.setNumber("bombs", GameState.getNumber("snap.bombs"));
  GameState.setNumber("power", GameState.getNumber("snap.power"));
}

export function addScore(points: f64): void {
  GameState.add("score", points);
}

export function savedHiscore(): f64 {
  return Save.getNumber("hiscore", 50000);
}

export function displayHiscore(): f64 {
  return Math.max(savedHiscore(), GameState.getNumber("score"));
}

// Persists the high score; returns true if the current score set a record.
export function commitHiscore(): bool {
  const score = GameState.getNumber("score");
  if (score > savedHiscore()) {
    Save.setNumber("hiscore", score);
    return true;
  }
  return false;
}

// ---- Audio --------------------------------------------------------------------

export function sfx(name: string, gain: f32 = 1.0): void {
  new Sound("assets/sounds/" + name + ".wav", Bus.Sfx).play(gain);
}

export function musicVolume(): f32 { return <f32>Save.getNumber("musicVolume", 0.6); }
export function sfxVolume(): f32 { return <f32>Save.getNumber("sfxVolume", 0.8); }

export function applySettings(): void {
  Audio.setVolume(Bus.Music, musicVolume());
  Audio.setVolume(Bus.Sfx, sfxVolume());
}

// ---- Visuals -----------------------------------------------------------------------

// The CRT filter is always added (so the options screen can toggle it live);
// it starts enabled per the saved setting.
export function addCrt(): PostEffect {
  const crt = PostEffect.custom("assets/shaders/crt.frag");
  crt.setUniform("u_strength", 1.0);
  crt.setEnabled(Save.getNumber("crt", 1) > 0);
  return crt;
}

export function setVisible(id: string, visible: bool): void {
  UI.toggleClass(id, "hidden", !visible);
}

// F11 / gamepad-free fullscreen toggle, remembered across runs.
export function handleGlobalKeys(): void {
  if (Input.pressed("fullscreen")) {
    const on = !Window.fullscreen;
    Window.fullscreen = on;
    Save.setNumber("fullscreen", on ? 1 : 0);
  }
}

export function transition(scene: string, shader: string = SHADER_WIPE, seconds: f32 = 0.8): void {
  if (!Scene.isTransitioning()) Scene.transition(scene, seconds, shader);
}

// ---- Spawning ------------------------------------------------------------------------

export function velocityJson(vx: f32, vy: f32): string {
  return '"VelocityComponent":{"velocity":[' + vx.toString() + "," + vy.toString() + "]}";
}

// Fires `prefab` from (x, y) toward `angle` (radians, 0 = +x, PI/2 = up).
export function shoot(prefab: string, x: f32, y: f32, angle: f32, speed: f32, rotate: bool = false): Entity {
  const vx = Mathf.cos(angle) * speed;
  const vy = Mathf.sin(angle) * speed;
  let json = "{" + velocityJson(vx, vy);
  if (rotate) json += ',"TransformComponent":{"rotation":' + (angle - PI / 2).toString() + "}";
  return World.spawn(prefab, x, y, json + "}");
}

export function angleTo(x: f32, y: f32, tx: f32, ty: f32): f32 {
  return Mathf.atan2(ty - y, tx - x);
}

export function explode(x: f32, y: f32, big: bool): void {
  World.spawn(big ? "assets/prefabs/explosion_big.prefab.json" : "assets/prefabs/explosion.prefab.json", x, y);
  const n = big ? 14 : 6;
  for (let i = 0; i < n; i++) {
    const a = rand(0, PI * 2);
    const s = rand(60, big ? 260 : 180);
    World.spawn("assets/prefabs/spark.prefab.json", x, y,
      "{" + velocityJson(Mathf.cos(a) * s, Mathf.sin(a) * s) + "}");
  }
}

// ---- Menus ---------------------------------------------------------------------------

// Vertical menu driven by the up/down/confirm actions. Items are element ids;
// the selected one gets the "selected" class.
export class Menu {
  index: i32 = 0;

  constructor(public items: string[]) {}

  render(): void {
    for (let i = 0; i < this.items.length; i++) {
      UI.toggleClass(this.items[i], "selected", i == this.index);
    }
  }

  // Returns the chosen index on confirm, -1 otherwise.
  update(): i32 {
    let moved = false;
    if (Input.pressed("up")) {
      this.index = (this.index + this.items.length - 1) % this.items.length;
      moved = true;
    }
    if (Input.pressed("down")) {
      this.index = (this.index + 1) % this.items.length;
      moved = true;
    }
    if (moved) {
      sfx("menu_move", 0.6);
      this.render();
    }
    if (Input.pressed("confirm")) {
      sfx("menu_select", 0.8);
      return this.index;
    }
    return -1;
  }
}

// ---- Shadows ----------------------------------------------------------------------------

// A plane's ground shadow: a dark copy of its sprite, drawn under everything
// that flies, offset down-right as if lit from the upper left. The owner
// calls follow() every frame and destroy() when it dies.
export class Shadow {
  private readonly entity: Entity;
  private readonly tr: TransformComponent = new TransformComponent();

  constructor(ship: string, scale: f32, private readonly dx: f32, private readonly dy: f32) {
    this.entity = World.spawn("assets/prefabs/shadow.prefab.json", 0, -2000,
      '{"SpriteComponent":{"texture":"assets/atlases/shmup.atlas.json#' + ship + '"},' +
      '"TransformComponent":{"scale":[' + scale.toString() + "," + scale.toString() + "]}}");
  }

  follow(x: f32, y: f32, rotation: f32): void {
    if (!this.tr.read(this.entity)) return;  // spawns at the end of the frame
    this.tr.x = x + this.dx;
    this.tr.y = y + this.dy;
    this.tr.rotation = rotation;
    this.tr.write(this.entity);
  }

  destroy(): void {
    this.entity.destroy();
  }
}
