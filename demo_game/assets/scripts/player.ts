

let t: f32 = 0;

export function onUpdate(dt: f32): void {
    t += dt;

    Inputs.keyIsPressed("A");
}
