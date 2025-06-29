// TEMP FOR NOW
@external("env", "log")
declare function log(ptr: i32, len: i32): void;

class console {
  static log(...messages: string[]): void {
    const message = messages.join("");
    const utf8 = String.UTF8.encode(message, true);
    const view = Uint8Array.wrap(utf8);
    log(<i32>view.dataStart, view.length - 1);
  }
}
// TEMP FOR NOW

let t: f32 = 0;

export function onUpdate(dt: f32): void {
  t += dt;
  console.log("dt: " + t.toString());
}
