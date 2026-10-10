import { Params, self } from "@jm/runtime";

const me = self();
const energy = me.light.energy;
const amount = <f32>Params.number("flicker", 0.06);
const speed = <f32>Params.number("speed", 2.4);
let phase = <f32>Params.number("phase");

// A bounded, gentle shimmer; independent of gameplay and its random state.
export function onUpdate(dt: f32): void {
  phase += dt * speed;
  me.light.energy = (energy * (1 + amount *
    (0.7 * Mathf.sin(phase) + 0.3 * Mathf.sin(phase * 2.3))));
}
