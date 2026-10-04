// Advance with the dt supplied to your script to respect its pause policy.
export class Timer {
  private left: f32;
  private armed: bool = false;

  // Starts ready unless a positive delay is supplied.
  constructor(seconds: f32 = 0) { this.left = Mathf.max(0, seconds); this.armed = seconds > 0; }
  get remaining(): f32 { return this.left; }
  get ready(): bool { return this.left <= 0; }
  start(seconds: f32): void { this.left = Mathf.max(0, seconds); this.armed = true; }
  // Extend a shield/flash without shortening a stronger existing request.
  extend(seconds: f32): void { this.start(Mathf.max(this.left, seconds)); }
  cancel(): void { this.left = 0; this.armed = false; }
  // True exactly once per start, on the tick that expires (including zero).
  tick(dt: f32): bool {
    this.left = Mathf.max(0, this.left - Mathf.max(0, dt));
    if (!this.armed || !this.ready) return false;
    this.armed = false;
    return true;
  }
}

export class TimedEvent<T> {
  constructor(readonly at: f32, readonly value: T) {}
}

// A reusable ordered sequence. Add events before advancing; equal-time events
// retain insertion order. Poll take() until it returns null to catch up fully.
export class Timeline<T> {
  private events: TimedEvent<T>[] = [];
  private next: i32 = 0;
  private clock: f32 = 0;
  private started: bool = false;

  get elapsed(): f32 { return this.clock; }
  get done(): bool { return this.next >= this.events.length; }
  at(seconds: f32, value: T): Timeline<T> {
    assert(!this.started, "Timeline: reset before adding events");
    const event = new TimedEvent<T>(seconds, value);
    let i = this.events.length;
    this.events.push(event);
    while (i > 0 && this.events[i - 1].at > seconds) {
      this.events[i] = this.events[i - 1];
      i--;
    }
    this.events[i] = event;
    return this;
  }
  advance(dt: f32): void { this.started = true; this.clock += Mathf.max(0, dt); }
  take(): TimedEvent<T> | null {
    if (this.done || this.events[this.next].at > this.clock) return null;
    return this.events[this.next++];
  }
  // Top-level functions can handle events directly (no capturing closures).
  update(dt: f32, handle: (value: T) => void): void {
    this.advance(dt);
    let event = this.take();
    while (event !== null) {
      handle(event.value);
      event = this.take();
    }
  }
  // Make all remaining events due; callers still process each exactly once.
  finish(): void {
    this.started = true;
    if (!this.done) this.clock = Mathf.max(this.clock, this.events[this.events.length - 1].at);
  }
  reset(): void { this.next = 0; this.clock = 0; this.started = false; }
}

// An impulse decaying linearly; overlapping requests keep the strongest.
export class Pulse {
  value: f32 = 0;
  constructor(readonly decay: f32) {}
  trigger(strength: f32 = 1): void { this.value = Mathf.max(this.value, strength); }
  tick(dt: f32): f32 {
    this.value = Mathf.max(0, this.value - Mathf.max(0, dt) * Mathf.max(0, this.decay));
    return this.value;
  }
}

// A fixed repeating interval that preserves fractional time across ticks.
// tick returns how many periods elapsed: loop that many times to catch up,
// or check > 0 to emit at most once after a slow frame.
export class Interval {
  private elapsed: f32 = 0;
  constructor(readonly seconds: f32) { assert(seconds > 0, "Interval: positive period required"); }
  tick(dt: f32): i32 {
    this.elapsed += Mathf.max(0, dt);
    const count = <i32>Mathf.floor(this.elapsed / this.seconds);
    this.elapsed -= <f32>count * this.seconds;
    return count;
  }
  reset(): void { this.elapsed = 0; }
}
