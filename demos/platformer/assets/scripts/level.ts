// Runs the current level (Session.level): builds it from its map, keeps the
// clock, HUD, music and pause, and moves on when Pip clears it or dies.
// Runs while paused (runWhenPaused) to unpause; everything else stops.
import { Input, Music, Overrides, Params, Renderer, Scene, Sound, TileMap, Time, UI, Window, formatNumber, spawn } from "@jm/runtime";
import { Theme, levelById, nextLevelId } from "./lib/levels";
import { Outcome, Session } from "./lib/session";
import { TILE, center, tileTag } from "./lib/tiles";

const ATLAS = "assets/atlases/sprites.atlas.json#";
const CLOCK_TICK: f32 = 0.4;   // seconds per clock unit
const TALLY_RATE: f32 = 120;   // clock units converted to score per second

// Params "level" ("1-3") and "startX" (a tile column) start anywhere: for testing and level design.
if (!Session.started) Session.newGame();  // launched directly (test scenes, JM_ENTRY_SCENE)
if (Params.text("level").length > 0) Session.level = Params.text("level");
const startX = <i32>Params.number("startX", -1);
const level = levelById(Session.level);
const theme = level.theme == Theme.Overworld ? "over_" : level.theme == Theme.Underground ? "under_" : "castle_";
const music = new Music(level.theme == Theme.Overworld ? "music_over" : level.theme == Theme.Underground ? "music_under" : "music_castle");
let clock: f32 = 0;
let tallying = false;     // converting the clock left into score
let tallied: f32 = 0;     // fractional clock units owed this frame
let leaveIn: f32 = -1;    // seconds until the next scene, or -1
let shownScore: f64 = -1, shownCoins = -1, shownTime = -1;
// The tiles: populated from the next frame, once the map entity exists.
const map = new TileMap(spawn("map", 0, 0, new Overrides().tag("map").text("TileMapComponent", "rows", level.tiles)
  .json("TileMapComponent", "vars", `{"theme": "${theme}"}`)));
let populated = false;

Session.beginAttempt();
Session.time = level.seconds;
if (level.theme == Theme.Overworld) Renderer.setClearColor(0.42, 0.62, 0.98);
else Renderer.setClearColor(0.02, 0.02, 0.04);
music.play(0.6);
UI.setText("world", level.id);

// Decoration `w` x `h` tiles, anchored at its bottom-left tile.
function deco(region: string, tx: i32, ty: i32, w: f32, h: f32): void {
  spawn("deco", (<f32>tx + w / 2) * TILE, (<f32>ty + h / 2) * TILE,
        new Overrides().texture(ATLAS + region).scale(w * TILE / 2, h * TILE / 2));
}

function block(prefab: string, kind: string, tx: i32, ty: i32): void {
  const o = new Overrides().paramText("kind", kind).param("tx", tx).param("ty", ty).tag(tileTag(tx, ty));
  if (kind == "B") o.texture(ATLAS + theme + "brick");
  spawn(prefab, center(tx), center(ty), o);
}

// Spawns what the map marks: blocks, coins, enemies, Pip, decorations.
function populate(): void {
  for (let ty = 0; ty < map.height; ty++) {
    for (let tx = 0; tx < map.width; tx++) {
      const c = map.at(tx, ty);
      const x = center(tx), y = center(ty);
      if (c == "B") block("brick", "B", tx, ty);
      else if (c == "?" || c == "M") block("block", c, tx, ty);
      else if (c == "o") spawn("coin", x, y);
      else if (c == "g") spawn("gloop", x, y);
      else if (c == "k") spawn("beetle", x, y);
      else if (c == "P") spawn("pip", startX >= 0 ? center(startX) : x, startX >= 0 ? center(8) : y);
      else if (c == "c") deco("cloud", tx, ty, 2, 1);
      else if (c == "h") deco("hill", tx, ty, 3, 1);
      else if (c == "b") deco("bush", tx, ty, 2, 1);
      else if (c == "C") deco("castle", tx, ty, 3, 3);
      else if (c == "|") pole(tx, ty);
      else if (c == "K") king(tx, ty);
    }
  }
}

// A 9-tile pole with a ball on top over its hard block (the map's "|"), the flag near the top.
function pole(tx: i32, ty: i32): void {
  for (let i = 1; i <= 9; i++) deco("pole", tx, ty + i, 1, 1);
  deco("pole_top", tx, ty + 10, 1, 1);
  spawn("flag", center(tx) - 8, center(ty + 9));
}

// The king guards the gem ("*"), which appears where it's marked once he falls.
function king(tx: i32, ty: i32): void {
  const o = new Overrides();
  for (let gy = 0; gy < map.height; gy++) {
    for (let gx = 0; gx < map.width; gx++) {
      if (map.at(gx, gy) == "*") o.param("gx", center(gx)).param("gy", center(gy));
    }
  }
  spawn("king", center(tx), <f32>ty * TILE + 16, o);
}

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
  if (!populated && map.width > 0) {  // the map exists from the frame after its spawn
    populated = true;
    populate();
  }
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
