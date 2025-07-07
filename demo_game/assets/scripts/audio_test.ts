let t: f32 = 0;

let played = false;
const sound = new Sound("acid-wool-cloth.wav");

export function onUpdate(dt: f32): void {
    t += dt;

    if (!played) {
        sound.play();
        played = true;
    }
}
