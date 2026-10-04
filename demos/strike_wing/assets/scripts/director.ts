// Runs a stage: the wave timeline, music, screen flashes, the intro banner
// and boss warning, then the exit to results, victory or game over.
// Param "stage": 1, 2 or 3 (the boss).
import { screen } from "./lib/presentation";
import { Audio, Pulse, fadeOut, Timer, blink, Music, Overrides, Params, PostEffect, UI, World, spawn } from "@jm/runtime";
import { stageWaves, Launch, BOSS_WARNING_AT } from "./lib/waves";
import * as Session from "./lib/session";
import { STAGE_COUNT, stageName } from "./lib/session";

const stage = <i32>Params.number("stage", 1);
const bossStage = stage == STAGE_COUNT;
const music = new Music("music_stage");
const bossMusic = new Music("music_boss");
const flashEffect = PostEffect.builtin("flash").setVec3("u_color", 1.0, 0.95, 0.8);
const launches = stageWaves(stage);
let t: f32 = 0;
const flash = new Pulse(2.2);
const exitDelay = new Timer();
let ending = false;
let warningShown = false;

if (!Session.score.present) Session.newGame();  // launched directly (JM_ENTRY_SCENE)
Session.beginStage(stage);
screen.open();
if (bossStage) PostEffect.builtin("vignette").setFloat("u_strength", 0.7);
music.play(0.9);
UI.setText("banner-sub", "STAGE " + stage.toString());
UI.setText("banner-title", stageName(stage));
screen.setVisible("banner", true);

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
  flash.tick(dt);
  flash.trigger(<f32>Session.flash.take());
  flashEffect.setFloat("u_amount", flash.value * 0.8);
}

function updateBanners(): void {
  if (t < 3.2) UI.opacity("banner", fadeOut(t, 2.4, 0.8));
  else screen.setVisible("banner", false);

  if (!bossStage) return;
  if (t >= BOSS_WARNING_AT && t < BOSS_WARNING_AT + 4.0) {
    if (!warningShown) {
      warningShown = true;
      screen.setVisible("warning", true);
      Audio.play("warning", 0.9);
      music.fadeOut(1.5);
    }
    UI.opacity("warning", blink(t - BOSS_WARNING_AT, 4, 1, 0.25));
  } else if (warningShown) {
    screen.setVisible("warning", false);
  }
}

function endStage(after: f32, fade: f32): void {
  ending = true;
  exitDelay.start(after);
  music.fadeOut(fade);
  bossMusic.fadeOut(fade);
}

function leave(): void {
  if (Session.gameOver.value) {
    screen.goTo("game_over", "dissolve");
  } else if (bossStage) {
    screen.goTo("victory", "dissolve");
  } else {
    Session.clearedStage.value = stage;
    screen.goTo("stage_clear", "dissolve");
  }
}

export function onUpdate(dt: f32): void {
  t += dt;
  screen.update();
  updateFlash(dt);
  updateBanners();
  launches.update(dt, launch);

  if (ending) {
    if (exitDelay.tick(dt)) leave();
    return;
  }
  if (Session.gameOver.value) {
    endStage(2.8, 2.0);
    return;
  }
  const cleared = bossStage ? Session.bossDefeated.value : launches.done && World.count("enemy") == 0;
  if (cleared) {
    Session.stageOver.value = true;
    endStage(bossStage ? 4.5 : 3.0, 2.5);
  }
}
