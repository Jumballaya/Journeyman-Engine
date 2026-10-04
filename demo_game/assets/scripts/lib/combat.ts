// Shots, explosions and plane shadows.
import { Entity, Overrides, Transform, spawn } from "@jm/runtime";
import { PI, rand } from "./util";

// Fires `prefab` from (x, y) toward `angle` (radians, 0 = right, PI/2 = up).
// `aligned` turns the sprite to face its direction of travel.
export function shoot(prefab: string, x: f32, y: f32, angle: f32, speed: f32, aligned: bool = false): Entity {
  const o = new Overrides().velocity(Mathf.cos(angle) * speed, Mathf.sin(angle) * speed);
  if (aligned) o.rotation(angle - PI / 2);
  return spawn(prefab, x, y, o);
}

// A fan of `count` shots spread evenly across `spread` radians around `angle`.
export function fan(prefab: string, x: f32, y: f32, angle: f32, spread: f32, count: i32, speed: f32): void {
  for (let i = 0; i < count; i++) {
    const offset = count > 1 ? spread * (<f32>i / <f32>(count - 1) - 0.5) : 0;
    shoot(prefab, x, y, angle + offset, speed);
  }
}

export function explode(x: f32, y: f32, big: bool): void {
  spawn(big ? "explosion_big" : "explosion", x, y);
  const sparks = big ? 14 : 6;
  for (let i = 0; i < sparks; i++) {
    const a = rand(0, PI * 2);
    const s = rand(60, big ? 260 : 180);
    spawn("spark", x, y, new Overrides().velocity(Mathf.cos(a) * s, Mathf.sin(a) * s));
  }
}

// A plane's ground shadow: a dark copy of its sprite under everything that
// flies, offset as if lit from the upper left. Call follow() every frame.
export class Shadow {
  private readonly transform: Transform;

  constructor(ship: string, scale: f32, private readonly dx: f32, private readonly dy: f32) {
    const entity = spawn("shadow", 0, -2000,
      new Overrides().texture("assets/atlases/shmup.atlas.json#" + ship).scale(scale, scale));
    this.transform = entity.transform;
  }

  follow(owner: Transform): void {
    this.transform.setPosition(owner.x + this.dx, owner.y + this.dy);
    this.transform.rotation = owner.rotation;
  }

  destroy(): void {
    this.transform.entity.destroy();
  }
}
