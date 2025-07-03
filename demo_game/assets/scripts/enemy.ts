export let t: f32 = 0;

let played = false;
let timer = 0;

export function onUpdate(dt: f32): void {
  t += dt;
  console.log("[enemy] dt: " + t.toString());

  if (!played && (timer > 3500)) {
    audio.playSound("acid-wool-cloth.wav");
    played = true;
  }
  timer++;
}
