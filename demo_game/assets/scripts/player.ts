let t: f32 = 0;

export function onUpdate(dt: f32): void {
  t += dt;
  console.log("dt: " + t.toString());
}
