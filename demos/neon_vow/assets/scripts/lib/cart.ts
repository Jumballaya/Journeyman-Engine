// Distance along the authored Path; waiting, acceleration, cruise and terminal brake.
export enum CartPhase { Waiting, Riding, Braking, Parked }
export const CRUISE: f32 = 360;
const ACCEL: f32 = 300;
const BRAKE: f32 = 220;

// Physics advances at most 1/20 s a frame (engine simulationStep); the sled keeps that clock.
export function physicsStep(dt: f32): f32 {
  return Mathf.min(dt, 1.0 / 20);
}

export class CartRun {
  along: f32 = 0;
  speed: f32 = 0;
  phase: CartPhase = CartPhase.Waiting;

  reset(): void {
    this.along = this.speed = 0;
    this.phase = CartPhase.Waiting;
  }

  tick(frameDt: f32, length: f32, aboard: bool): void {
    const dt = physicsStep(frameDt);
    if (this.phase == CartPhase.Waiting && aboard) this.phase = CartPhase.Riding;
    if (this.phase == CartPhase.Waiting || this.phase == CartPhase.Parked || dt <= 0) return;
    const remaining = Mathf.max(0, length - this.along);
    const limit = Mathf.sqrt(2 * BRAKE * remaining);
    if (limit < CRUISE) this.phase = CartPhase.Braking;
    this.speed = Mathf.min(CRUISE, Mathf.min(this.speed + ACCEL * dt, limit));
    this.along = Mathf.min(length, this.along + this.speed * dt);
    if (length - this.along < 0.05) {
      this.along = length;
      this.speed = 0;
      this.phase = CartPhase.Parked;
    }
  }
}
