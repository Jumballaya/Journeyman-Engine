import { clamp } from "./math";

// How a tween moves between its ends over time.
export enum Ease { Linear, InQuad, OutQuad, InOutQuad, OutCubic, OutBack, OutBounce }

// Where `kind` is at t in [0, 1] (0 at the start, 1 at the end; OutBack overshoots).
export function ease(kind: Ease, t: f32): f32 {
  t = clamp(t, 0, 1);
  switch (kind) {
    case Ease.InQuad: return t * t;
    case Ease.OutQuad: return t * (2 - t);
    case Ease.InOutQuad: return t < 0.5 ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t);
    case Ease.OutCubic: { const u: f32 = 1 - t; return 1 - u * u * u; }
    case Ease.OutBack: { const u: f32 = t - 1; return 1 + u * u * (2.70158 * u + 1.70158); }
    case Ease.OutBounce: {
      if (t < 1 / 2.75) return 7.5625 * t * t;
      if (t < 2 / 2.75) { const u: f32 = t - 1.5 / 2.75; return 7.5625 * u * u + 0.75; }
      if (t < 2.5 / 2.75) { const u: f32 = t - 2.25 / 2.75; return 7.5625 * u * u + 0.9375; }
      const u: f32 = t - 2.625 / 2.75;
      return 7.5625 * u * u + 0.984375;
    }
    default: return t;
  }
}

// A number going from `from` to `to` over `seconds`: tick it with the script's dt.
export class Tween {
  private elapsed: f32 = 0;
  constructor(public from: f32, public to: f32, readonly seconds: f32, readonly kind: Ease = Ease.Linear) {}
  get value(): f32 {
    const t = this.seconds > 0 ? this.elapsed / this.seconds : 1;
    return this.from + (this.to - this.from) * ease(this.kind, t);
  }
  get done(): bool { return this.elapsed >= this.seconds; }
  tick(dt: f32): f32 {
    this.elapsed = Mathf.min(this.seconds, this.elapsed + Mathf.max(0, dt));
    return this.value;
  }
  // From the start again; retarget also gives new ends.
  restart(): void { this.elapsed = 0; }
  retarget(from: f32, to: f32): void { this.from = from; this.to = to; this.elapsed = 0; }
}
