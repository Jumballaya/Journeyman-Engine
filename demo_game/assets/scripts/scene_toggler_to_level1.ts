// Press N to crossfade-transition from level2 back to level1.

export function onUpdate(dt: f32): void {
  if (Inputs.keyIsPressed(Key.N) && !Scene.isTransitioning()) {
    Scene.transition("scenes/level1.scene.json", 0.75);
  }
}
