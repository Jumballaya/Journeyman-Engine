// Press N to crossfade-transition from level1 to level2.

import { Scene, Inputs, Key } from "@jm/runtime";

export function onUpdate(dt: f32): void {
  if (Inputs.keyIsPressed(Key.N) && !Scene.isTransitioning()) {
    Scene.transition("scenes/level2.scene.json", 0.75);
  }
}
