// Runs a stage: the wave timeline, music, screen flashes, the intro banner
// and boss warning, then the exit to results, victory or game over.
// Param "stage": 1, 2 or 3 (the boss).
import { Music, Overrides, Params, PostEffect, UI, World, spawn } from "@jm/runtime";
import { blink, sfx } from "./lib/util";
import { Session, STAGE_COUNT, stageName } from "./lib/session";
import { addCrt, handleGlobalKeys } from "./lib/settings";
import { goTo, setVisible } from "./lib/screens";

// `count` planes of `prefab`, one every `every` seconds, starting at `at`
// seconds into the stage; each plane starts dx further along x.
class Wave {
  at: f32 = 0;
  prefab: string = "";
  count: i32 = 1;
  every: f32 = 0;
  x: f32 = 0;
  dx: f32 = 0;
  y: f32 = 360;
  pattern: string = "straight";
  dir: f32 = 1;
}

const STAGE_1: Wave[] = [
  { at: 3.0, prefab: "enemy_fighter", count: 5, x: -160, dx: 80 },
  { at: 7.0, prefab: "enemy_fighter", count: 5, every: 0.35, x: -170, pattern: "swoop" },
  { at: 11.0, prefab: "enemy_fighter", count: 5, every: 0.35, x: 170, pattern: "swoop", dir: -1 },
  { at: 15.0, prefab: "enemy_zero", count: 4, every: 0.5, x: -120, dx: 80, pattern: "sine" },
  { at: 19.0, prefab: "enemy_fighter", count: 6, every: 0.3, x: -90, pattern: "loop" },
  { at: 23.0, prefab: "enemy_gunship", pattern: "hover" },
  { at: 24.0, prefab: "enemy_fighter", count: 4, every: 0.4, x: -200 },
  { at: 24.2, prefab: "enemy_fighter", count: 4, every: 0.4, x: 200 },
  { at: 31.0, prefab: "enemy_zero", count: 4, every: 0.25, x: -150, dx: 100, pattern: "dive" },
  { at: 35.0, prefab: "enemy_fighter", count: 5, every: 0.3, x: -260, y: 220, pattern: "side" },
  { at: 38.0, prefab: "enemy_ace", x: -150, pattern: "swoop" },
  { at: 38.5, prefab: "enemy_ace", x: 150, pattern: "swoop", dir: -1 },
  { at: 43.0, prefab: "enemy_bomber", x: -110, pattern: "hover" },
  { at: 46.0, prefab: "enemy_bomber", x: 110, pattern: "hover" },
  { at: 52.0, prefab: "enemy_zero", count: 8, every: 0.4, x: -140, dx: 40, pattern: "sine" },
  { at: 58.0, prefab: "enemy_fighter", count: 6, every: 0.3, x: 90, pattern: "loop", dir: -1 },
  { at: 62.0, prefab: "enemy_ace", count: 2, every: 0.5, x: -120, dx: 240, pattern: "dive" },
  { at: 66.0, prefab: "enemy_gunship", x: -100, pattern: "hover" },
  { at: 66.5, prefab: "enemy_gunship", x: 100, pattern: "hover" },
  { at: 68.0, prefab: "enemy_fighter", count: 6, every: 0.35, x: 260, y: 240, pattern: "side", dir: -1 },
];

const STAGE_2: Wave[] = [
  { at: 3.0, prefab: "enemy_zero", count: 5, every: 0.2, x: -160, dx: 80 },
  { at: 6.5, prefab: "enemy_ace", count: 3, every: 0.4, x: -170, pattern: "swoop" },
  { at: 9.5, prefab: "enemy_ace", count: 3, every: 0.4, x: 170, pattern: "swoop", dir: -1 },
  { at: 13.0, prefab: "enemy_gunship", x: -120, pattern: "hover" },
  { at: 13.5, prefab: "enemy_gunship", x: 120, pattern: "hover" },
  { at: 16.0, prefab: "enemy_zero", count: 6, every: 0.3, x: -260, y: 250, pattern: "side" },
  { at: 19.0, prefab: "enemy_zero", count: 6, every: 0.3, x: 260, y: 200, pattern: "side", dir: -1 },
  { at: 24.0, prefab: "enemy_fighter", count: 8, every: 0.25, x: -100, pattern: "loop" },
  { at: 26.0, prefab: "enemy_fighter", count: 8, every: 0.25, x: 100, pattern: "loop", dir: -1 },
  { at: 30.0, prefab: "enemy_bomber", pattern: "hover" },
  { at: 31.0, prefab: "enemy_zero", count: 6, every: 0.3, x: -180, dx: 72, pattern: "dive" },
  { at: 38.0, prefab: "enemy_ace", count: 4, every: 0.35, x: -150, dx: 100, pattern: "sine" },
  { at: 42.0, prefab: "enemy_gunship", count: 3, every: 0.6, x: -150, dx: 150, pattern: "hover" },
  { at: 48.0, prefab: "enemy_zero", count: 10, every: 0.2, x: -200, dx: 44, pattern: "dive" },
  { at: 54.0, prefab: "enemy_ace", count: 3, every: 0.35, x: -170, pattern: "loop" },
  { at: 55.0, prefab: "enemy_ace", count: 3, every: 0.35, x: 170, pattern: "loop", dir: -1 },
  { at: 60.0, prefab: "enemy_bomber", x: -120, pattern: "hover" },
  { at: 61.0, prefab: "enemy_bomber", x: 120, pattern: "hover" },
  { at: 64.0, prefab: "enemy_fighter", count: 8, every: 0.3, x: -260, y: 260, pattern: "side" },
  { at: 70.0, prefab: "enemy_ace", count: 6, every: 0.3, x: -150, dx: 60, pattern: "dive" },
  { at: 75.0, prefab: "enemy_gunship", count: 2, every: 0.5, x: -90, dx: 180, pattern: "hover" },
];

const BOSS_AT: f32 = 17.0;
const STAGE_3: Wave[] = [
  { at: 3.0, prefab: "enemy_zero", count: 4, every: 0.3, x: -150, dx: 100 },
  { at: 6.0, prefab: "enemy_fighter", count: 5, every: 0.3, x: -170, pattern: "swoop" },
  { at: 8.5, prefab: "enemy_fighter", count: 5, every: 0.3, x: 170, pattern: "swoop", dir: -1 },
  { at: BOSS_AT, prefab: "boss" },
];
const BOSS_WARNING_AT: f32 = BOSS_AT - 4.5;

// One plane to launch.
class Launch {
  constructor(readonly at: f32, readonly wave: Wave, readonly x: f32) {}
}

const stage = <i32>Params.number("stage", 1);
const bossStage = stage == STAGE_COUNT;
const music = new Music("music_stage");
const bossMusic = new Music("music_boss");
const flashEffect = PostEffect.builtin("flash").setVec3("u_color", 1.0, 0.95, 0.8);
const launches = timeline(stage == 1 ? STAGE_1 : stage == 2 ? STAGE_2 : STAGE_3);
let nextLaunch: i32 = 0;
let t: f32 = 0;
let flash: f32 = 0;
let exitAt: f32 = -1;  // stage time to leave at, once the stage has ended
let left = false;
let warningShown = false;

if (!Session.started) Session.newGame();  // launched directly (JM_ENTRY_SCENE)
Session.beginStage(stage);
addCrt();
if (bossStage) PostEffect.builtin("vignette").setFloat("u_strength", 0.7);
music.play(0.9);
UI.setText("banner-sub", "STAGE " + stage.toString());
UI.setText("banner-title", stageName(stage));
setVisible("banner", true);

function timeline(waves: Wave[]): Launch[] {
  const out = new Array<Launch>();
  for (let w = 0; w < waves.length; w++) {
    const wave = waves[w];
    for (let i = 0; i < wave.count; i++) {
      out.push(new Launch(wave.at + <f32>i * wave.every, wave, wave.x + <f32>i * wave.dx));
    }
  }
  return out.sort((a: Launch, b: Launch): i32 => a.at < b.at ? -1 : a.at > b.at ? 1 : 0);
}

function launch(l: Launch): void {
  if (l.wave.prefab == "boss") {
    spawn("boss", l.x, l.wave.y);
    bossMusic.play(0.9);
    return;
  }
  spawn(l.wave.prefab, l.x, l.wave.y,
        new Overrides().paramText("pattern", l.wave.pattern).param("dir", l.wave.dir));
}

function updateFlash(dt: f32): void {
  flash = Mathf.max(Mathf.max(0, flash - dt * 2.2), <f32>Session.takeFlash());
  flashEffect.setFloat("u_amount", flash * 0.8);
}

function updateBanners(): void {
  if (t < 3.2) UI.setStyle("banner", "opacity", (t < 2.4 ? 1.0 : 1.0 - (t - 2.4) / 0.8).toString());
  else setVisible("banner", false);

  if (!bossStage) return;
  if (t >= BOSS_WARNING_AT && t < BOSS_WARNING_AT + 4.0) {
    if (!warningShown) {
      warningShown = true;
      setVisible("warning", true);
      sfx("warning", 0.9);
      music.fadeOut(1.5);
    }
    UI.setStyle("warning", "opacity", blink(t - BOSS_WARNING_AT, 4) ? "1" : "0.25");
  } else if (warningShown) {
    setVisible("warning", false);
  }
}

function endStage(after: f32, fade: f32): void {
  exitAt = t + after;
  music.fadeOut(fade);
  bossMusic.fadeOut(fade);
}

function leave(): void {
  if (Session.gameOver) {
    goTo("game_over", "dissolve");
  } else if (bossStage) {
    goTo("victory", "dissolve");
  } else {
    Session.clearedStage = stage;
    goTo("stage_clear", "dissolve");
  }
}

export function onUpdate(dt: f32): void {
  t += dt;
  handleGlobalKeys();
  updateFlash(dt);
  updateBanners();
  while (nextLaunch < launches.length && launches[nextLaunch].at <= t) launch(launches[nextLaunch++]);

  if (exitAt >= 0) {
    if (t >= exitAt && !left) {
      left = true;
      leave();
    }
    return;
  }
  if (Session.gameOver) {
    endStage(2.8, 2.0);
    return;
  }
  const cleared = bossStage ? Session.bossDefeated : nextLaunch >= launches.length && World.count("enemy") == 0;
  if (cleared) {
    Session.stageOver = true;
    endStage(bossStage ? 4.5 : 3.0, 2.5);
  }
}
