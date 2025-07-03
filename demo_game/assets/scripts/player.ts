export let t: f32 = 0;

export function onUpdate(dt: f32): void {
  t += dt;
  console.log("[player] dt: " + t.toString());
  audio.playSound("acid-wool-cloth.wav");
}
