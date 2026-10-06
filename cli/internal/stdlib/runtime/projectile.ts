import { Entity } from "./entity";
import { spawn, Overrides } from "./world";
import { PI, TAU } from "./math";
import { Random } from "./random";

// A prefab fired at a fixed speed. Angles are radians from +x. For oriented
// art, frontAngle describes the direction the unrotated sprite faces.
export class Projectile {
  constructor(readonly prefab: string, readonly speed: f32, readonly rotate: bool = false, readonly frontAngle: f32 = PI / 2) {}
  fire(x: f32, y: f32, angle: f32): Entity { return this.launch(x, y, angle, this.speed); }
  // Count shots including both ends of a total angular spread; one is centered.
  fan(x: f32, y: f32, angle: f32, count: i32, spread: f32): void {
    for (let i = 0; i < count; i++) {
      const offset: f32 = count == 1 ? 0 : spread * (<f32>i / <f32>(count - 1) - 0.5);
      this.fire(x, y, angle + offset);
    }
  }
  ring(x: f32, y: f32, count: i32, offset: f32 = 0): void {
    for (let i = 0; i < count; i++) this.fire(x, y, offset + TAU * <f32>i / <f32>count);
  }
  // Random directions/speeds, useful for debris and sparks. Speed is the max.
  scatter(x: f32, y: f32, count: i32, minSpeed: f32 = 0): void {
    for (let i = 0; i < count; i++) this.launch(x, y, Random.range(0, TAU), Random.range(minSpeed, this.speed));
  }
  private launch(x: f32, y: f32, angle: f32, speed: f32): Entity {
    const overrides = new Overrides().velocity(Mathf.cos(angle) * speed, Mathf.sin(angle) * speed);
    if (this.rotate) overrides.rotation(angle - this.frontAngle);
    return spawn(this.prefab, x, y, overrides);
  }
}
