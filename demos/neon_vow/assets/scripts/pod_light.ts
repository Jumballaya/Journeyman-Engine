import { Params, self } from "@jm/runtime";

const me = self();
const energy = me.light.energy;
const standby = <f32>Params.number("standby", 0.25);

export function onUpdate(dt: f32): void {
  me.light.energy = energy * (me.parent.data.getBool("active") ? 1 : standby);
}
