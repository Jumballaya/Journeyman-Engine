export let t: f32 = 0;

let played = false;

let i = 1;

const sound = new Sound("acid-wool-cloth.wav");

export function onUpdate(dt: f32): void {
  t += dt;

  if (!played) {
    sound.play();
    sound.fadeOut(1);
    played = true;
  }

  if (i % 2 === 0) {
    console.log("[player] dt: " + t.toString());
    sound.gain = 1.0;
    sound.stop();
  }
  i += 2;
}
