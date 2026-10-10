import { self } from "@jm/runtime";

const me = self();
const bladeX = me.local.x;
const energy = me.light.energy;

export function onUpdate(dt: f32): void {
  const hero = me.parent;
  // Child positions don't inherit sprite scale; mirror the authored blade offset.
  me.local.x = hero.transform.scaleX < 0 ? -bladeX : bladeX;
  me.light.energy = hero.sprite.alpha > 0 ? energy : 0;
}
