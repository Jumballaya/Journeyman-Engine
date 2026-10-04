// The current run, shared by every script through GameState: score, ships,
// stage progress and the signals gameplay scripts send each other.
import { GameState, Save } from "@jm/runtime";

export const STAGE_COUNT = 3;
const STAGE_SCENES = ["level1", "level2", "boss"];
const STAGE_NAMES = ["PACIFIC DAWN", "JUNGLE FRONT", "IRON FORTRESS"];

// Saved at a stage's first attempt so "restart stage" can roll back.
const CHECKPOINTED = ["score", "lives", "bombs", "power"];
// Cleared whenever a stage (re)starts.
const PER_STAGE = [
  "kills", "shots", "hits", "deaths", "stageOver", "gameOver", "bossActive", "bossDefeated", "bossHealth", "flash",
];

function num(key: string, fallback: f64 = 0): f64 { return GameState.getNumber(key, fallback); }
function flag(key: string): bool { return GameState.getNumber(key) > 0; }
function setFlag(key: string, on: bool): void { GameState.setNumber(key, on ? 1 : 0); }

export class Session {
  // False when a stage scene is launched directly (JM_ENTRY_SCENE).
  static get started(): bool { return GameState.has("score"); }

  static get score(): f64 { return num("score"); }
  static addScore(points: f64): void { GameState.add("score", points); }

  // Reserve ships: the run ends when a ship is lost with none left.
  static get lives(): i32 { return <i32>num("lives"); }
  static set lives(n: i32) { GameState.setNumber("lives", min(n, 5)); }
  static get bombs(): i32 { return <i32>num("bombs"); }
  static set bombs(n: i32) { GameState.setNumber("bombs", min(n, 5)); }
  static get power(): i32 { return <i32>num("power", 1); }  // 1..3
  static set power(n: i32) { GameState.setNumber("power", max(1, min(n, 3))); }

  static get stage(): i32 { return <i32>num("stage", 1); }

  // This stage's statistics, for the results screen.
  static get kills(): i32 { return <i32>num("kills"); }
  static get shots(): i32 { return <i32>num("shots"); }
  static get hits(): i32 { return <i32>num("hits"); }
  static get deaths(): i32 { return <i32>num("deaths"); }
  static countKill(): void { GameState.add("kills", 1); }
  static countShots(n: i32): void { GameState.add("shots", n); }
  static countHit(): void { GameState.add("hits", 1); }
  static countDeath(): void { GameState.add("deaths", 1); }

  static get stageOver(): bool { return flag("stageOver"); }
  static set stageOver(on: bool) { setFlag("stageOver", on); }
  static get gameOver(): bool { return flag("gameOver"); }
  static set gameOver(on: bool) { setFlag("gameOver", on); }

  // Published by the boss for the HUD and the director.
  static get bossActive(): bool { return flag("bossActive"); }
  static set bossActive(on: bool) { setFlag("bossActive", on); }
  static get bossDefeated(): bool { return flag("bossDefeated"); }
  static set bossDefeated(on: bool) { setFlag("bossDefeated", on); }
  static get bossHealth(): f64 { return num("bossHealth"); }  // 0..1
  static set bossHealth(f: f64) { GameState.setNumber("bossHealth", f); }

  // A white screen flash (0..1) any script can request; the director shows it.
  static flash(amount: f64): void { GameState.setNumber("flash", Math.max(amount, num("flash"))); }
  static takeFlash(): f64 {
    const f = num("flash");
    GameState.setNumber("flash", 0);
    return f;
  }

  // The stage just cleared, for the results screen.
  static get clearedStage(): i32 { return <i32>num("clearedStage", 1); }
  static set clearedStage(n: i32) { GameState.setNumber("clearedStage", n); }

  static newGame(): void {
    GameState.clear();
    GameState.setNumber("score", 0);
    GameState.setNumber("stage", 1);
    Session.resetShips();
  }

  // Continue after game over: full ships at the current stage, score reset.
  static continueGame(): void {
    GameState.setNumber("score", 0);
    GameState.remove("checkpointStage");
    Session.resetShips();
  }

  static beginStage(stage: i32): void {
    GameState.setNumber("stage", stage);
    if (num("checkpointStage") != <f64>stage) {
      GameState.setNumber("checkpointStage", stage);
      for (let i = 0; i < CHECKPOINTED.length; i++) {
        GameState.setNumber("checkpoint." + CHECKPOINTED[i], num(CHECKPOINTED[i]));
      }
    }
    for (let i = 0; i < PER_STAGE.length; i++) GameState.remove(PER_STAGE[i]);
  }

  // Rolls back to the start of the current stage; reload its scene next.
  static restartStage(): void {
    for (let i = 0; i < CHECKPOINTED.length; i++) {
      GameState.setNumber(CHECKPOINTED[i], num("checkpoint." + CHECKPOINTED[i]));
    }
  }

  private static resetShips(): void {
    GameState.setNumber("lives", 2);
    GameState.setNumber("bombs", 3);
    GameState.setNumber("power", 1);
  }
}

export function stageScene(stage: i32): string { return STAGE_SCENES[clampStage(stage) - 1]; }
export function stageName(stage: i32): string { return STAGE_NAMES[clampStage(stage) - 1]; }
function clampStage(stage: i32): i32 { return max(1, min(stage, STAGE_COUNT)); }

// ---- High score (saved) ---------------------------------------------------------------

export function hiscore(): f64 { return Save.getNumber("hiscore", 50000); }

// What the HUD shows: the record, or this run's score once it beats it.
export function liveHiscore(): f64 { return Math.max(hiscore(), Session.score); }

// Saves the score if it is a record; true if it was.
export function recordHiscore(): bool {
  if (Session.score <= hiscore()) return false;
  Save.setNumber("hiscore", Session.score);
  return true;
}
