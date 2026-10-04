// Authored stage formations. Timeline owns ordering and dispatch; this file
// chooses which aircraft appear, their formation and their flight patterns.
// `count` planes of `prefab`, one every `every` seconds, starting at `at`
// seconds into the stage; each plane starts dx further along x.
import { Timeline } from "@jm/runtime";

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

export const BOSS_AT: f32 = 17.0;
const STAGE_3: Wave[] = [
  { at: 3.0, prefab: "enemy_zero", count: 4, every: 0.3, x: -150, dx: 100 },
  { at: 6.0, prefab: "enemy_fighter", count: 5, every: 0.3, x: -170, pattern: "swoop" },
  { at: 8.5, prefab: "enemy_fighter", count: 5, every: 0.3, x: 170, pattern: "swoop", dir: -1 },
  { at: BOSS_AT, prefab: "boss" },
];
export const BOSS_WARNING_AT: f32 = BOSS_AT - 4.5;


export class Launch {
  constructor(readonly wave: Wave, readonly x: f32) {}
}

export function stageWaves(stage: i32): Timeline<Launch> {
  const waves = stage == 1 ? STAGE_1 : stage == 2 ? STAGE_2 : STAGE_3;
  const timeline = new Timeline<Launch>();
  for (let w = 0; w < waves.length; w++) {
    const wave = waves[w];
    for (let i = 0; i < wave.count; i++) {
      timeline.at(wave.at + <f32>i * wave.every, new Launch(wave, wave.x + <f32>i * wave.dx));
    }
  }
  return timeline;
}
