import {
  __jmTimeScale, __jmTimeSetScale, __jmTimeElapsed, __jmTimeUnscaledElapsed, __jmTimeUnscaledDelta,
} from "./env";

// Game time. onUpdate's dt is already scaled (unscaled for runWhenPaused scripts).
export class Time {
  // 1 = normal speed. 0 pauses: gameplay scripts, physics and animation stop.
  static get scale(): f32 { return __jmTimeScale(); }
  static set scale(s: f32) { __jmTimeSetScale(s); }

  static get paused(): bool { return __jmTimeScale() == 0; }
  static pause(): void { __jmTimeSetScale(0); }
  static resume(): void { __jmTimeSetScale(1); }

  static get elapsed(): f64 { return __jmTimeElapsed(); }  // scaled seconds
  static get unscaledElapsed(): f64 { return __jmTimeUnscaledElapsed(); }
  static get unscaledDelta(): f32 { return __jmTimeUnscaledDelta(); }
}
