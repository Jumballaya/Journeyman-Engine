// Runs a level scene (everything in it is authored): keeps the clock, HUD,
// music and pause, and moves on when Pip clears it or dies.
// Runs while paused (runWhenPaused) to unpause; everything else stops.
import { Input, Music, Params, Renderer, Scene, Sound, Time, UI, Window, formatNumber } from "@jm/runtime";
import { Theme, levelById, nextLevelId } from "./lib/levels";
import { Outcome, Session } from "./lib/session";

const CLOCK_TICK: f32 = 0.4;   // seconds per clock unit
const TALLY_RATE: f32 = 120;   // clock units converted to score per second

if (!Session.started) Session.newGame();  // launched directly (JM_ENTRY_SCENE)
Session.level = Params.text("level", Session.level);  // param "level": this scene's level id
const level = levelById(Session.level);
const music = new Music(level.theme == Theme.Overworld ? "music_over" : level.theme == Theme.Underground ? "music_under" : "music_castle");
let clock: f32 = 0;
let tallying = false;     // converting the clock left into score
let tallied: f32 = 0;     // fractional clock units owed this frame
let leaveIn: f32 = -1;    // seconds until the next scene, or -1
let shownScore: f64 = -1, shownCoins = -1, shownTime = -1;

Session.beginAttempt();
Session.time = level.seconds;
if (level.theme == Theme.Overworld) Renderer.setClearColor(0.42, 0.62, 0.98);
else Renderer.setClearColor(0.02, 0.02, 0.04);
music.play(0.6);
UI.setText("world", level.id);

function updateHud(): void {
  if (Session.score != shownScore) UI.setText("score", formatNumber(shownScore = Session.score, 6));
  if (Session.coins != shownCoins) UI.setText("coins", formatNumber(shownCoins = Session.coins, 2));
  if (Session.time != shownTime) UI.setText("time", formatNumber(shownTime = Session.time, 3));
}

function leave(): void {
  if (Session.outcome == Outcome.Dead) {
    Session.lives--;
    Session.big = false;
    Scene.load(Session.lives > 0 ? "intro" : "game_over");
    return;
  }
  const next = nextLevelId(level.id);
  if (next.length == 0) {
    Scene.transition("victory", 1.0);
  } else {
    Session.level = next;
    Scene.load("intro");
  }
}

function togglePause(): void {
  if (Time.paused) Time.resume();
  else Time.pause();
  UI.setVisible("pause", Time.paused, "hidden");
  music.gain = Time.paused ? 0.15 : 0.6;
}

export function onUpdate(dt: f32): void {
  const playing = Session.outcome == Outcome.Playing;
  if (playing && (Input.pressed("pause") || (!Window.focused && !Time.paused))) togglePause();
  if (Time.paused) return;
  updateHud();
  if (leaveIn >= 0) {
    leaveIn -= dt;
    if (leaveIn < 0) leave();
    return;
  }
  if (Session.outcome == Outcome.Dead) {
    leaveIn = 0;
    return;
  }
  if (Session.outcome == Outcome.Clear) {
    if (!tallying) {
      tallying = true;
      new Sound("clear").play(0.7);
    }
    tallied += TALLY_RATE * dt;
    const units = min(<i32>tallied, Session.time);
    tallied -= <f32>units;
    Session.time = Session.time - units;
    Session.addScore(<f64>units * 50);
    if (Session.time == 0) leaveIn = 2.5;
    return;
  }
  clock += dt;
  if (clock >= CLOCK_TICK && Session.time > 0) {
    clock -= CLOCK_TICK;
    Session.time = Session.time - 1;
    if (Session.time == 100) new Sound("oneup").play(0.5);  // hurry-up chime
  }
}
