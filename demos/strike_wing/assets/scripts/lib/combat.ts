// Strike Wing's explosion recipes.
import { Projectile, spawn } from "@jm/runtime";

const sparks = new Projectile("spark", 180);
const bigSparks = new Projectile("spark", 260);
export function explode(x: f32, y: f32, big: bool): void {
  spawn(big ? "explosion_big" : "explosion", x, y);
  if (big) bigSparks.scatter(x, y, 14, 60);
  else sparks.scatter(x, y, 6, 60);
}
