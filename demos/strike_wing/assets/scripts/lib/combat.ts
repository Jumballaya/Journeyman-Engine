// Strike Wing's explosion recipes and plane shadow artwork.
import { Overrides, Projectile, TransformFollower, spawn } from "@jm/runtime";

const sparks = new Projectile("spark", 180);
const bigSparks = new Projectile("spark", 260);
export function explode(x: f32, y: f32, big: bool): void {
  spawn(big ? "explosion_big" : "explosion", x, y);
  if (big) bigSparks.scatter(x, y, 14, 60);
  else sparks.scatter(x, y, 6, 60);
}

export class Shadow extends TransformFollower {
  constructor(ship: string, scale: f32, dx: f32, dy: f32) {
    super(spawn("shadow", 0, -2000,
      new Overrides().texture("assets/atlases/shmup.atlas.json#" + ship).scale(scale, scale)), dx, dy);
  }
}
