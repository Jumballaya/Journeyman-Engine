// Strike Wing's playfield, score presentation and default sound bus.
import { Sound, formatNumber, PI } from "@jm/runtime";

export const HALF_W: f32 = 240;
export const HALF_H: f32 = 320;
export const UP: f32 = PI / 2;
export const DOWN: f32 = -PI / 2;
export function scoreText(score: f64): string { return formatNumber(score, 7); }
export function sfx(name: string, gain: f32 = 1): void { new Sound(name).play(gain); }
