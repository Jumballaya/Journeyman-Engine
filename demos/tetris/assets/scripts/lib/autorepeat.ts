// Delayed auto-shift: a held direction steps once immediately, then again
// after `delay`, then every `rate` seconds.
import { Input } from "@jm/runtime";

export class AutoRepeat {
  private held: f32 = -1;  // seconds the action has been down, -1 = up

  constructor(readonly action: string, readonly delay: f32 = 0.16, readonly rate: f32 = 0.05) {}

  // Steps to take this frame.
  update(dt: f32): i32 {
    if (!Input.down(this.action)) {
      this.held = -1;
      return 0;
    }
    if (this.held < 0) {
      this.held = 0;
      return 1;
    }
    const before = this.held;
    this.held += dt;
    return this.repeatsBy(this.held) - this.repeatsBy(before);
  }

  private repeatsBy(t: f32): i32 {
    return t < this.delay ? 0 : 1 + <i32>((t - this.delay) / this.rate);
  }
}
