import {
  __jmTimeScale, __jmTimeSetScale, __jmTimeElapsed,
  __jmTimeUnscaledElapsed, __jmTimeUnscaledDelta,
} from "./env";

// Game time. onUpdate(dt) already receives scaled dt (or unscaled dt for
// runWhenPaused scripts); these cover everything else.
export class Time {
  // 1 = normal, 0 = paused (gameplay scripts stop updating, physics freezes).
  static get scale(): f32 { return __jmTimeScale(); }
  static set scale(s: f32) { __jmTimeSetScale(s); }

  static get paused(): bool { return __jmTimeScale() == 0; }
  static pause(): void { __jmTimeSetScale(0); }
  static resume(): void { __jmTimeSetScale(1); }

  // Scaled seconds since start (stops while paused).
  static get elapsed(): f64 { return __jmTimeElapsed(); }
  // Real seconds since start.
  static get unscaledElapsed(): f64 { return __jmTimeUnscaledElapsed(); }
  static get unscaledDelta(): f32 { return __jmTimeUnscaledDelta(); }
}

// @deprecated use the dt passed to onUpdate.
export function getDeltaTime(): f32 {
  return __jmTimeUnscaledDelta() * __jmTimeScale();
}
