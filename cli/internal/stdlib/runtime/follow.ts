import { Entity, Transform } from "./entity";
import { clamp } from "./math";
import { Camera } from "./render";

// An explicitly owned companion (marker, escort, etc). Call follow each frame
// and destroy when the owner dies. Scene unload also destroys the companion.
export class TransformFollower {
  private readonly transform: Transform;
  constructor(readonly entity: Entity, readonly dx: f32 = 0, readonly dy: f32 = 0, readonly copyRotation: bool = true) {
    this.transform = entity.transform;
  }
  follow(target: Transform): void {
    // Prefab components arrive at the end of the spawn frame.
    if (!this.entity.has("TransformComponent") || !target.entity.has("TransformComponent")) return;
    this.transform.setPosition(target.x + this.dx, target.y + this.dy);
    if (this.copyRotation) this.transform.rotation = target.rotation;
  }
  destroy(): void { this.entity.destroy(); }
}

// Damage once per live source (e.g. overlapping bomb areas). Uses the full
// generation-safe handle; recycled entity indices count as new sources.
export class HitHistory {
  private sources: Entity[] = [];
  accept(source: Entity): bool {
    if (!source.isAlive) return false;
    for (let i = this.sources.length - 1; i >= 0; i--) {
      if (!this.sources[i].isAlive) this.sources.splice(i, 1);
      else if (this.sources[i].equals(source)) return false;
    }
    this.sources.push(source);
    return true;
  }
  clear(): void { this.sources.length = 0; }
}

// Keeps the camera on a target the way platformers do: the target moves freely
// inside a dead zone, the view eases after it, looks ahead the way it's going,
// and stays inside bounds. Call follow(player, dt) each frame.
export class CameraFollow {
  deadZoneWidth: f32 = 0;
  deadZoneHeight: f32 = 0;
  smoothing: f32 = 8;   // per second: higher catches up faster; 0 snaps
  lookAhead: f32 = 0;   // world units ahead of a target moving sideways
  private ahead: f32 = 0;
  private baseX: f32;  // where it follows the target to, before looking ahead and bounds
  private baseY: f32;
  private bounded: bool = false;
  private minX: f32 = 0;
  private minY: f32 = 0;
  private maxX: f32 = 0;
  private maxY: f32 = 0;

  constructor(public x: f32 = 0, public y: f32 = 0) {
    this.baseX = x;
    this.baseY = y;
  }

  // The part of the world the view stays inside (a level's edges).
  setBounds(minX: f32, minY: f32, maxX: f32, maxY: f32): CameraFollow {
    this.minX = minX; this.minY = minY; this.maxX = maxX; this.maxY = maxY;
    this.bounded = true;
    return this;
  }

  follow(target: Entity, dt: f32): void {
    if (!target.has("TransformComponent")) return;
    const vx = target.has("VelocityComponent") ? target.velocity.x : <f32>0;
    this.update(dt, target.transform.x, target.transform.y, vx, Camera.width, Camera.height);
    Camera.setPosition(this.x, this.y);
  }

  // The view's center after dt, for a target at (tx, ty) moving at vx, with a
  // view of viewWidth x viewHeight world units (for the bounds).
  update(dt: f32, tx: f32, ty: f32, vx: f32 = 0, viewWidth: f32 = 0, viewHeight: f32 = 0): void {
    const k: f32 = this.smoothing > 0 ? 1 - Mathf.exp(-this.smoothing * Mathf.max(0, dt)) : 1;
    const wantAhead: f32 = vx > 0 ? this.lookAhead : vx < 0 ? -this.lookAhead : this.ahead;
    // Each eases on its own, so the sum doesn't depend on how frames fall.
    this.ahead += (wantAhead - this.ahead) * k;
    this.baseX += (CameraFollow.keep(this.baseX, tx, this.deadZoneWidth * 0.5) - this.baseX) * k;
    this.baseY += (CameraFollow.keep(this.baseY, ty, this.deadZoneHeight * 0.5) - this.baseY) * k;
    this.x = this.baseX + this.ahead;
    this.y = this.baseY;
    if (!this.bounded) return;
    this.x = CameraFollow.inside(this.x, this.minX, this.maxX, viewWidth * 0.5);
    this.y = CameraFollow.inside(this.y, this.minY, this.maxY, viewHeight * 0.5);
  }

  // Where the center goes so `at` is within `half` of it.
  private static keep(center: f32, at: f32, half: f32): f32 {
    return clamp(center, at - half, at + half);
  }
  // A center keeping a view `half` wide inside [lo, hi] (centered if it's wider).
  private static inside(center: f32, lo: f32, hi: f32, half: f32): f32 {
    return hi - lo <= half * 2 ? (lo + hi) * 0.5 : clamp(center, lo + half, hi - half);
  }
}
