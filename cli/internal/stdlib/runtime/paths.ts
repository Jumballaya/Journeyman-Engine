import { Vec2, clamp } from "./math";
import { MapObject } from "./tiles";

// A line to follow: a cart's rail, a patrol, a lift's route. World units;
// places on it are distances from its first point.
export class Path {
  private xs: f32[] = [];
  private ys: f32[] = [];
  private starts: f32[] = [];  // each segment's distance from the start
  private total: f32 = 0;

  // points: x then y for each; closed: the last joins the first (two points: there and back).
  constructor(points: StaticArray<f32>, readonly closed: bool = false) {
    for (let i = 0; i + 1 < points.length; i += 2) this.add(points[i], points[i + 1]);
    if (closed && this.xs.length > 1) this.add(this.xs[0], this.ys[0]);
    for (let i = 0; i + 1 < this.xs.length; i++) {
      this.starts.push(this.total);
      this.total += Mathf.sqrt((this.xs[i + 1] - this.xs[i]) ** 2 + (this.ys[i + 1] - this.ys[i]) ** 2);
    }
  }
  // A Tiled polyline or polygon (map.objects("rail")[0]).
  static fromObject(o: MapObject): Path { return new Path(o.points, o.closed); }

  get length(): f32 { return this.total; }

  // The point `distance` along it: clamped to its ends, or around again if closed.
  at(distance: f32, out: Vec2): Vec2 {
    if (this.xs.length == 0) return out.set(0, 0);
    const i = this.segment(distance);
    if (i < 0) return out.set(this.xs[0], this.ys[0]);
    const t = this.fraction(i, distance);
    return out.set(this.xs[i] + (this.xs[i + 1] - this.xs[i]) * t, this.ys[i] + (this.ys[i + 1] - this.ys[i]) * t);
  }
  // Which way it goes there, as a unit vector (a cart's tilt: Mathf.atan2(out.y, out.x)).
  direction(distance: f32, out: Vec2): Vec2 {
    const i = this.segment(distance);
    if (i < 0) return out.set(1, 0);
    const dx = this.xs[i + 1] - this.xs[i], dy = this.ys[i + 1] - this.ys[i];
    const l = Mathf.sqrt(dx * dx + dy * dy);
    return l > 0 ? out.set(dx / l, dy / l) : out.set(1, 0);
  }
  // How far along it the point nearest (x, y) is: to put something onto it.
  nearest(x: f32, y: f32): f32 {
    let best: f32 = 0, bestDistance: f32 = Infinity;
    for (let i = 0; i < this.starts.length; i++) {
      const dx = this.xs[i + 1] - this.xs[i], dy = this.ys[i + 1] - this.ys[i];
      const l2 = dx * dx + dy * dy;
      const t: f32 = l2 > 0 ? clamp(((x - this.xs[i]) * dx + (y - this.ys[i]) * dy) / l2, 0, 1) : 0;
      const px = this.xs[i] + dx * t - x, py = this.ys[i] + dy * t - y;
      const d = px * px + py * py;
      if (d < bestDistance) { bestDistance = d; best = this.starts[i] + Mathf.sqrt(l2) * t; }
    }
    return best;
  }

  private add(x: f32, y: f32): void {
    const n = this.xs.length;
    if (n > 0 && this.xs[n - 1] == x && this.ys[n - 1] == y) return;  // no zero-length segments
    this.xs.push(x);
    this.ys.push(y);
  }
  // The segment holding `distance` (-1: no segments).
  private segment(distance: f32): i32 {
    if (this.starts.length == 0) return -1;
    const d = this.wrap(distance);
    let i = this.starts.length - 1;
    while (i > 0 && this.starts[i] > d) i--;
    return i;
  }
  private fraction(i: i32, distance: f32): f32 {
    const end = i + 1 < this.starts.length ? this.starts[i + 1] : this.total;
    const span = end - this.starts[i];
    return span > 0 ? clamp((this.wrap(distance) - this.starts[i]) / span, 0, 1) : 0;
  }
  private wrap(distance: f32): f32 {
    if (!this.closed || this.total <= 0) return clamp(distance, 0, this.total);
    const d = distance % this.total;
    return d < 0 ? d + this.total : d;
  }
}

// Something swinging on a rope from an anchor: a vine, a hook. Attach with the
// body's position and velocity (that measures the rope; it keeps its momentum),
// tick, and let go with velocityX/Y. Angles are from straight down, counter-clockwise.
export class Swing {
  angle: f32 = 0;
  speed: f32 = 0;  // radians per second
  length: f32 = 1;
  constructor(public anchorX: f32, public anchorY: f32, public gravity: f32 = 900) {}

  attach(x: f32, y: f32, vx: f32 = 0, vy: f32 = 0): void {
    const dx = x - this.anchorX, dy = y - this.anchorY;
    const l = Mathf.sqrt(dx * dx + dy * dy);
    if (l < 1e-3) return;  // on the anchor itself: no rope to measure
    this.length = l;
    this.angle = Mathf.atan2(dx, -dy);
    // The part of its velocity along the swing's way (perpendicular to the rope).
    this.speed = (vx * Mathf.cos(this.angle) + vy * Mathf.sin(this.angle)) / this.length;
  }
  // `push`: radians per second per second the player adds (pumping the swing).
  tick(dt: f32, push: f32 = 0): void {
    // Steps well inside its period (sqrt(length / gravity)) keep a short, fast swing steady.
    const period = Mathf.sqrt(Mathf.max(this.length, 1e-3) / Mathf.max(Mathf.abs(this.gravity), 1e-3));
    const steps = <i32>Mathf.min(256, Mathf.max(4, Mathf.ceil(Mathf.max(0, dt) / (0.05 * period))));
    const h = Mathf.max(0, dt) / <f32>steps;
    for (let i = 0; i < steps; i++) {
      this.speed += (-this.gravity / this.length * Mathf.sin(this.angle) + push) * h;
      this.angle += this.speed * h;
    }
  }
  get x(): f32 { return this.anchorX + this.length * Mathf.sin(this.angle); }
  get y(): f32 { return this.anchorY - this.length * Mathf.cos(this.angle); }
  get velocityX(): f32 { return this.speed * this.length * Mathf.cos(this.angle); }
  get velocityY(): f32 { return this.speed * this.length * Mathf.sin(this.angle); }
}
