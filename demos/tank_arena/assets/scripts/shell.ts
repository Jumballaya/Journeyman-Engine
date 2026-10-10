// A shell, simulated by the server: whatever it hits first takes it. Its
// controller is the player whose tank fired it.
import { Entity, self } from "@jm/runtime";

const me = self();
let spent = false;

export function onUpdate(dt: f32): void {}

export function onOverlap(other: Entity): void {
  if (spent) return;
  if (other.hasTag("tank")) {
    if (other.data.getBool("wrecked")) return;
    other.send("hit", "", me.controller);
  }
  spent = true;
  me.destroy();
}
