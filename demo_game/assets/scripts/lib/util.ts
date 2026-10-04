// Math, text and sound helpers shared by every script.
import { Sound } from "@jm/runtime";

// The playfield is the 480x640 logical screen: origin at the center, y up.
export const HALF_W: f32 = 240;
export const HALF_H: f32 = 320;
export const PI: f32 = Mathf.PI;
export const UP: f32 = PI / 2;
export const DOWN: f32 = -PI / 2;

export function rand(a: f32, b: f32): f32 {
  return a + <f32>Math.random() * (b - a);
}

// Inclusive of both ends.
export function randInt(a: i32, b: i32): i32 {
  return a + <i32>Math.floor(Math.random() * <f64>(b - a + 1));
}

export function pick(items: string[]): string {
  return items[randInt(0, items.length - 1)];
}

export function clamp(v: f32, lo: f32, hi: f32): f32 {
  return v < lo ? lo : v > hi ? hi : v;
}

// Moves v toward target by `rate` (0..1) of the remaining distance.
export function approach(v: f32, target: f32, rate: f32): f32 {
  return v + (target - v) * clamp(rate, 0, 1);
}

export function angleTo(x: f32, y: f32, tx: f32, ty: f32): f32 {
  return Mathf.atan2(ty - y, tx - x);
}

// Alternates every 1/rate seconds, for prompts and warnings.
export function blink(t: f32, rate: f32 = 2): bool {
  return <i32>Mathf.floor(t * rate) % 2 == 0;
}

// Zero-padded score text ("0012340").
export function pad(n: f64, digits: i32 = 7): string {
  const s = (<i64>Math.max(0, Math.floor(n))).toString();
  return s.length >= digits ? s : "0".repeat(digits - s.length) + s;
}

export function percent(fraction: f64): string {
  return (<i32>Math.round(Math.max(0, Math.min(1, fraction)) * 100)).toString() + "%";
}

export function sfx(name: string, gain: f32 = 1): void {
  new Sound(name).play(gain);
}
