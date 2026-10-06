// World-space math uses radians, +x at zero, +y at PI/2.
export const PI: f32 = Mathf.PI;
export const TAU: f32 = 2 * Mathf.PI;

export function clamp(value: f32, lo: f32, hi: f32): f32 {
  return Mathf.max(lo, Mathf.min(hi, value));
}

export function lerp(from: f32, to: f32, weight: f32): f32 {
  return from + (to - from) * clamp(weight, 0, 1);
}

export function angleTo(x: f32, y: f32, targetX: f32, targetY: f32): f32 {
  return Mathf.atan2(targetY - y, targetX - x);
}

// Shortest signed rotation, in [-PI, PI].
export function angleDifference(from: f32, to: f32): f32 {
  let delta = (to - from) % TAU;
  if (delta > PI) delta -= TAU;
  if (delta < -PI) delta += TAU;
  return delta;
}

export function turnTowards(from: f32, to: f32, maxRadians: f32): f32 {
  const step = Mathf.max(0, maxRadians);
  return from + clamp(angleDifference(from, to), -step, step);
}

// Reusable mutable vector: keep one around for frame-by-frame input/math.
export class Vec2 {
  constructor(public x: f32 = 0, public y: f32 = 0) {}
  set(x: f32, y: f32): Vec2 { this.x = x; this.y = y; return this; }
  get length(): f32 { return Mathf.sqrt(this.x * this.x + this.y * this.y); }
  limit(maxLength: f32 = 1): Vec2 {
    const length = this.length;
    const limit = Mathf.max(0, maxLength);
    if (length > limit) { this.x *= limit / length; this.y *= limit / length; }
    return this;
  }
}

// Explicit edges, independent of the renderer's logical screen dimensions.
export class Rect {
  constructor(readonly left: f32, readonly bottom: f32, readonly right: f32, readonly top: f32) {}
  contains(x: f32, y: f32): bool {
    return x >= this.left && x <= this.right && y >= this.bottom && y <= this.top;
  }
  clampX(x: f32): f32 { return clamp(x, this.left, this.right); }
  clampY(y: f32): f32 { return clamp(y, this.bottom, this.top); }
}

// Alternates twice per cycle. `rate` is the number of switches per second.
export function blink(elapsed: f32, rate: f32, high: f32 = 1, low: f32 = 0): f32 {
  return <i32>Mathf.floor(elapsed * rate) % 2 == 0 ? high : low;
}

export function fadeOut(elapsed: f32, hold: f32, duration: f32): f32 {
  return duration <= 0 ? (elapsed < hold ? 1 : 0) : 1 - clamp((elapsed - hold) / duration, 0, 1);
}
