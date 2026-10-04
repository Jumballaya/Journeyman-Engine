// Runs a stage: spawns the wave timeline, owns music and screen effects,
// shows the intro banner / boss warning, and moves on when the stage is
// cleared (stage_clear or victory) or the player runs out of ships.
// Param "stage": 1, 2 or 3 (boss).
import {
  World, GameState, Params, Sound, Bus, PostEffect, BuiltinEffect, UI,
} from "@jm/runtime";
import {
  beginStage, stageName, setVisible, addCrt, handleGlobalKeys, transition, sfx,
  SCENE_CLEAR, SCENE_GAME_OVER, SCENE_VICTORY, SHADER_DISSOLVE,
} from "./lib/game";

class Wave {
  constructor(
    public t: f32, public enemy: string, public count: i32, public interval: f32,
    public x: f32, public dx: f32, public pattern: string, public dir: f32 = 1, public y: f32 = 360) {}
}

class Spawn {
  constructor(public t: f32, public prefab: string, public x: f32, public y: f32, public overrides: string) {}
}

// Formation shorthand: x is the first plane's x, dx the step between planes.
function W(t: f32, enemy: string, count: i32, interval: f32, x: f32, dx: f32, pattern: string,
           dir: f32 = 1, y: f32 = 360): Wave {
  return new Wave(t, enemy, count, interval, x, dx, pattern, dir, y);
}

function stage1(): Wave[] {
  return [
    W(3.0, "fighter", 5, 0.0, -160, 80, "straight"),
    W(7.0, "fighter", 5, 0.35, -170, 0, "swoop", 1),
    W(11.0, "fighter", 5, 0.35, 170, 0, "swoop", -1),
    W(15.0, "zero", 4, 0.5, -120, 80, "sine"),
    W(19.0, "fighter", 6, 0.3, -90, 0, "loop", 1),
    W(23.0, "gunship", 1, 0, 0, 0, "hover"),
    W(24.0, "fighter", 4, 0.4, -200, 0, "straight"),
    W(24.2, "fighter", 4, 0.4, 200, 0, "straight"),
    W(31.0, "zero", 4, 0.25, -150, 100, "dive"),
    W(35.0, "fighter", 5, 0.3, -260, 0, "side", 1, 220),
    W(38.0, "ace", 1, 0, -150, 0, "swoop", 1),
    W(38.5, "ace", 1, 0, 150, 0, "swoop", -1),
    W(43.0, "bomber", 1, 0, -110, 0, "hover"),
    W(46.0, "bomber", 1, 0, 110, 0, "hover"),
    W(52.0, "zero", 8, 0.4, -140, 40, "sine"),
    W(58.0, "fighter", 6, 0.3, 90, 0, "loop", -1),
    W(62.0, "ace", 2, 0.5, -120, 240, "dive"),
    W(66.0, "gunship", 1, 0, -100, 0, "hover"),
    W(66.5, "gunship", 1, 0, 100, 0, "hover"),
    W(68.0, "fighter", 6, 0.35, 260, 0, "side", -1, 240),
  ];
}

function stage2(): Wave[] {
  return [
    W(3.0, "zero", 5, 0.2, -160, 80, "straight"),
    W(6.5, "ace", 3, 0.4, -170, 0, "swoop", 1),
    W(9.5, "ace", 3, 0.4, 170, 0, "swoop", -1),
    W(13.0, "gunship", 1, 0, -120, 0, "hover"),
    W(13.5, "gunship", 1, 0, 120, 0, "hover"),
    W(16.0, "zero", 6, 0.3, -260, 0, "side", 1, 250),
    W(19.0, "zero", 6, 0.3, 260, 0, "side", -1, 200),
    W(24.0, "fighter", 8, 0.25, -100, 0, "loop", 1),
    W(26.0, "fighter", 8, 0.25, 100, 0, "loop", -1),
    W(30.0, "bomber", 1, 0, 0, 0, "hover"),
    W(31.0, "zero", 6, 0.3, -180, 72, "dive"),
    W(38.0, "ace", 4, 0.35, -150, 100, "sine"),
    W(42.0, "gunship", 3, 0.6, -150, 150, "hover"),
    W(48.0, "zero", 10, 0.2, -200, 44, "dive"),
    W(54.0, "ace", 3, 0.35, -170, 0, "loop", 1),
    W(55.0, "ace", 3, 0.35, 170, 0, "loop", -1),
    W(60.0, "bomber", 1, 0, -120, 0, "hover"),
    W(61.0, "bomber", 1, 0, 120, 0, "hover"),
    W(64.0, "fighter", 8, 0.3, -260, 0, "side", 1, 260),
    W(70.0, "ace", 6, 0.3, -150, 60, "dive"),
    W(75.0, "gunship", 2, 0.5, -90, 180, "hover"),
  ];
}

function stage3(): Wave[] {
  return [
    W(3.0, "zero", 4, 0.3, -150, 100, "straight"),
    W(6.0, "fighter", 5, 0.3, -170, 0, "swoop", 1),
    W(8.5, "fighter", 5, 0.3, 170, 0, "swoop", -1),
    W(17.0, "boss", 1, 0, 0, 0, "boss"),
  ];
}

const BOSS_WARNING_AT: f32 = 12.5;

const music = new Sound("assets/sounds/music_stage.wav", Bus.Music);
const bossMusic = new Sound("assets/sounds/music_boss.wav", Bus.Music);
let stage: i32 = 1;
let spawns: Spawn[] = [];
let nextSpawn: i32 = 0;
let t: f32 = 0;
let started = false;
let flashFx: PostEffect | null = null;
let flashAmount: f32 = 0;
let endTimer: f32 = -1;
let warningShown = false;

function buildTimeline(): void {
  const waves = stage == 1 ? stage1() : stage == 2 ? stage2() : stage3();
  for (let w = 0; w < waves.length; w++) {
    const wave = waves[w];
    for (let i = 0; i < wave.count; i++) {
      const overrides = wave.pattern == "boss"
        ? ""
        : '{"ScriptComponent":{"params":{"pattern":"' + wave.pattern + '","dir":' + wave.dir.toString() + "}}}";
      const prefab = "assets/prefabs/" + (wave.enemy == "boss" ? "boss" : "enemy_" + wave.enemy) + ".prefab.json";
      spawns.push(new Spawn(wave.t + <f32>i * wave.interval, prefab,
                            wave.x + <f32>i * wave.dx, wave.y, overrides));
    }
  }
  spawns.sort((a: Spawn, b: Spawn): i32 => a.t < b.t ? -1 : a.t > b.t ? 1 : 0);
}

function start(): void {
  started = true;
  stage = <i32>Params.number("stage", 1);
  beginStage(stage);
  buildTimeline();
  addCrt();
  if (stage == 3) new PostEffect(BuiltinEffect.Vignette).setUniform("u_strength", 0.7);
  const flash = new PostEffect(BuiltinEffect.Flash);
  flash.setUniformVec3("u_color", 1.0, 0.95, 0.8);
  flashFx = flash;
  music.play(0.9, true);

  UI.setText("banner-sub", "STAGE " + stage.toString());
  UI.setText("banner-title", stageName(stage));
  setVisible("banner", true);
}

function updateFlash(dt: f32): void {
  const requested = <f32>GameState.getNumber("flash", 0);
  if (requested > 0) {
    flashAmount = Mathf.max(flashAmount, requested);
    GameState.setNumber("flash", 0);
  }
  flashAmount = Mathf.max(0, flashAmount - dt * 2.2);
  const fx = flashFx;
  if (fx !== null) fx.setUniform("u_amount", flashAmount * 0.8);
}

function updateBanner(): void {
  if (t < 3.2) {
    const fade = t < 2.4 ? 1.0 : 1.0 - (t - 2.4) / 0.8;
    UI.setStyle("banner", "opacity", fade.toString());
  } else {
    setVisible("banner", false);
  }
  if (stage == 3 && t >= BOSS_WARNING_AT && t < BOSS_WARNING_AT + 4.0) {
    if (!warningShown) {
      warningShown = true;
      setVisible("warning", true);
      sfx("warning", 0.9);
      music.fadeOut(1.5);
    }
    UI.setStyle("warning", "opacity", Mathf.floor((t - BOSS_WARNING_AT) * 4) % 2 == 0 ? "1" : "0.25");
  } else if (warningShown) {
    setVisible("warning", false);
  }
}

function finish(scene: string): void {
  transition(scene, SHADER_DISSOLVE, 1.0);
}

export function onUpdate(dt: f32): void {
  if (!started) start();
  t += dt;
  handleGlobalKeys();
  updateFlash(dt);
  updateBanner();

  while (nextSpawn < spawns.length && spawns[nextSpawn].t <= t) {
    const s = spawns[nextSpawn++];
    World.spawn(s.prefab, s.x, s.y, s.overrides);
    if (s.prefab.includes("boss")) bossMusic.play(0.9, true);
  }

  if (endTimer >= 0) {
    endTimer -= dt;
    if (endTimer < 0) {
      if (GameState.getNumber("gameOver") > 0) {
        finish(SCENE_GAME_OVER);
      } else if (stage == 3) {
        finish(SCENE_VICTORY);
      } else {
        GameState.setNumber("clearedStage", stage);
        finish(SCENE_CLEAR);
      }
      endTimer = 1000;  // transition requested; don't fire again
    }
    return;
  }

  if (GameState.getNumber("gameOver") > 0) {
    endTimer = 2.8;
    music.fadeOut(2.0);
    bossMusic.fadeOut(2.0);
    return;
  }

  const timelineDone = nextSpawn >= spawns.length;
  const cleared = stage == 3
    ? GameState.getNumber("bossDefeated") > 0
    : timelineDone && World.count("enemy") == 0;
  if (cleared) {
    GameState.setNumber("stageOver", 1);
    endTimer = stage == 3 ? 4.5 : 3.0;
    music.fadeOut(2.5);
    bossMusic.fadeOut(3.0);
  }
}
