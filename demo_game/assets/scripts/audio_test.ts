import { Sound } from "@jm/runtime";

let t: f32 = 0;

let played = false;
const sound = new Sound("guitar.ogg");

export function onUpdate(dt: f32): void {
    t += dt;

    if (!played) {
        sound.play();
        played = true;
    }
}
