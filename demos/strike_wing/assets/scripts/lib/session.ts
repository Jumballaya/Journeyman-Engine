// Strike Wing's run schema and rules. Storage, bounds, reset groups and
// checkpoints are supplied by jm; every script shares these live handles.
import { Session, StateNumber, Save } from "@jm/runtime";

export const STAGE_COUNT = 3;
const STAGE_SCENES = ["level1", "level2", "boss"];
const STAGE_NAMES = ["PACIFIC DAWN", "JUNGLE FRONT", "IRON FORTRESS"];
const run = new Session();
const ships = run.group();
const stats = run.group();
export const score = run.number<f64>("score", 0);
export const stage = run.number<i32>("stage", 1);
export const clearedStage = run.number<i32>("clearedStage", 1);
export const lives = ships.number<i32>("lives", 2, -1, 5);
export const bombs = ships.number<i32>("bombs", 3, 0, 5);
export const power = ships.number<i32>("power", 1, 1, 3);
export const kills = stats.number<i32>("kills", 0);
export const shots = stats.number<i32>("shots", 0);
export const hits = stats.number<i32>("hits", 0);
export const deaths = stats.number<i32>("deaths", 0);
export const stageOver = stats.flag("stageOver");
export const gameOver = stats.flag("gameOver");
export const bossActive = stats.flag("bossActive");
export const bossDefeated = stats.flag("bossDefeated");
export const bossHealth = stats.number<f64>("bossHealth", 0, 0, 1);
export const flash = stats.number<f64>("flash", 0, 0, 1);
const checkpoint = run.checkpoint("checkpoint", [score, lives, bombs, power]);
const best = new StateNumber<f64>(Save, "hiscore", 50000);

export function newGame(): void {
  run.reset();
  checkpoint.forget();
  score.value = 0; // also marks the run as started
}
export function continueGame(): void {
  score.value = 0;
  ships.reset();
  checkpoint.forget();
}
export function beginStage(number: i32): void {
  stage.value = number;
  checkpoint.captureOnce(number);
  stats.reset();
}
export function restartStage(): void { checkpoint.restore(); }
export function stageScene(stage: i32): string { return STAGE_SCENES[max(1, min(stage, STAGE_COUNT)) - 1]; }
export function stageName(stage: i32): string { return STAGE_NAMES[max(1, min(stage, STAGE_COUNT)) - 1]; }
export function hiscore(): f64 { return best.value; }
export function liveHiscore(): f64 { return Math.max(best.value, score.value); }
export function recordHiscore(): bool { return best.record(score.value); }
