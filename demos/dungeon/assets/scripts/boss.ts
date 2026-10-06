// Ogloth: drifts around its chamber in a figure eight and spits fire at the
// hero; faster when hurt. Eight hits; then the shard appears (params sx, sy).
import { Camera, Entity, Overrides, Params, World, spawn } from "@jm/runtime";
import { Foe } from "./lib/foe";
import { Session } from "./lib/session";

const HEALTH = 8;
const ORB_SPEED: f32 = 90;

const foe = new Foe(HEALTH, 13, true);
const homeX = foe.body.x, homeY = foe.body.y;
let t: f32 = 0;
let fireIn: f32 = 2;

function fire(): void {
  const hero = World.find("hero");
  if (hero.isNone) return;
  const aim = Mathf.atan2(hero.transform.y - foe.body.y, hero.transform.x - foe.body.x);
  for (let i = -1; i <= 1; i++) {
    const a = aim + <f32>i * 0.3;
    spawn("orb", foe.body.x, foe.body.y, new Overrides().velocity(Mathf.cos(a) * ORB_SPEED, Mathf.sin(a) * ORB_SPEED));
  }
}

export function onUpdate(dt: f32): void {
  const angry = foe.health <= HEALTH / 2;
  t += dt * (angry ? 1.6 : 1);
  const tx = homeX - 40 + Mathf.sin(t * 0.9) * 48, ty = homeY + Mathf.sin(t * 1.8) * 36;
  foe.update(dt, (tx - foe.body.x) * 2 * dt, (ty - foe.body.y) * 2 * dt);
  fireIn -= dt;
  if (fireIn <= 0) {
    fireIn = angry ? 1.1 : 1.8;
    fire();
  }
}

export function onCollide(other: Entity): void {
  if (!other.hasTag("sword")) return;
  if (!foe.hitBy(other, "boss_hit")) return;
  Camera.shake(12, 0.6);
  Session.markDone("boss");
  spawn("shard", <f32>Params.number("sx"), <f32>Params.number("sy"));
}
