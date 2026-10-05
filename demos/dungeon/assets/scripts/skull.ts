// A skeleton: walks in straight lines, turning at walls; when the hero lines
// up with it, it charges. Two hits.
import { Entity, Random, World } from "@jm/runtime";
import { Foe } from "./lib/foe";

const WALK: f32 = 35;
const CHARGE: f32 = 95;

const foe = new Foe(2, 6);
let dirX: f32 = 1, dirY: f32 = 0;
let turnIn: f32 = Random.range(1, 3);

function pickDirection(): void {
  const d = Random.int(0, 3);
  dirX = d == 0 ? 1 : d == 1 ? -1 : 0;
  dirY = d == 2 ? 1 : d == 3 ? -1 : 0;
  turnIn = Random.range(1, 3);
}

export function onUpdate(dt: f32): void {
  turnIn -= dt;
  if (turnIn <= 0) pickDirection();
  let speed = WALK;
  const hero = World.find("hero");
  if (!hero.isNone) {
    const hx = hero.transform.x - foe.body.x, hy = hero.transform.y - foe.body.y;
    if (Mathf.abs(hx) < 6 && Mathf.abs(hy) < 96) { dirX = 0; dirY = hy > 0 ? 1 : -1; speed = CHARGE; }
    else if (Mathf.abs(hy) < 6 && Mathf.abs(hx) < 96) { dirX = hx > 0 ? 1 : -1; dirY = 0; speed = CHARGE; }
  }
  foe.update(dt, dirX * speed * dt, dirY * speed * dt);
  if (foe.body.blocked) pickDirection();
  foe.me.transform.scaleX = dirX < 0 ? -8 : 8;
}

export function onCollide(other: Entity): void {
  if (other.hasTag("sword")) foe.hitBy(other);
}
