// The round's rules, on the host only (the director is shared: its script
// runs where it's simulated). Pellets appear, the clock runs down, the most
// pellets wins, then everyone goes back to the lobby.
import { GameState, Net, Overrides, Random, Scene, Timer, World, spawn } from "@jm/runtime";
import { PHASE, TIME_LEFT, WINNER, scoreKey } from "./lib/players";

const ROUND: f64 = 40;
const RESULTS: f64 = 5;
const MAX_PELLETS = 22;
const spawnTimer = new Timer();

// A fresh round, unless this host is taking over one already running.
if (GameState.getString(PHASE) == "start") {
  for (let p = 0; p < 8; p++) GameState.remove(scoreKey(p));
  GameState.setNumber(TIME_LEFT, ROUND);
  GameState.setString(PHASE, "play");
}

function spawnPellet(): void {
  const x = <f32>Random.range(-280, 280);
  const y = <f32>Random.range(-150, 120);
  if (Random.chance(0.12)) {
    spawn("pellet", x, y, new Overrides().tint(1, 0.82, 0.28).scale(7, 7).param("value", 5));
  } else {
    spawn("pellet", x, y);
  }
}

function leader(): i32 {
  const players = Net.players();
  let best = -1;
  let bestScore: f64 = -1;
  for (let i = 0; i < players.length; i++) {
    const score = GameState.getNumber(scoreKey(players[i]));
    if (score > bestScore) {
      bestScore = score;
      best = players[i];
    }
  }
  return best;
}

export function onUpdate(dt: f32): void {
  const phase = GameState.getString(PHASE);
  const left = GameState.getNumber(TIME_LEFT) - dt;
  GameState.setNumber(TIME_LEFT, Math.max(0, left));
  if (phase == "play") {
    if (spawnTimer.tick(dt) || spawnTimer.ready) {
      spawnTimer.start(<f32>Random.range(0.25, 0.6));
      if (World.findAll("pellet").length < MAX_PELLETS) spawnPellet();
    }
    if (left <= 0) {
      GameState.setNumber(WINNER, leader());
      GameState.setString(PHASE, "results");
      GameState.setNumber(TIME_LEFT, RESULTS);
      const pellets = World.findAll("pellet");
      for (let i = 0; i < pellets.length; i++) pellets[i].destroy();
    }
  } else if (phase == "results" && left <= 0) {
    Scene.load("lobby");
  }
}
