import { Logger } from "@jm/runtime";

let t: f32 = 0;

export function onUpdate(dt: f32): void {
    t += dt;
    Logger.log("[console_test] dt: " + t.toString());
}
