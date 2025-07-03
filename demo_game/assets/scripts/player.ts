export let t: f32 = 0;

let played = false;

export function onUpdate(dt: f32): void {
  t += dt;
  console.log("[player] dt: " + t.toString());

  if (!played) {
    audio.playSound("thud.ogg");
    played = true;
  }
}
