@external("env", "jmLog")
declare function jmLog(ptr: i32, len: i32): void;

@external("env", "get_component")
declare function _get_component(entityId: i32, typePtr: i32, typeLen: i32): i32;

@external("env", "playSound")
declare function playSoundNative(ptr: i32, len: i32): void;

export namespace audio {
  export function playSound(name: string): void {
    const utf8 = String.UTF8.encode(name, true);
    const view = Uint8Array.wrap(utf8);
    playSoundNative(view.dataStart, view.length - 1);
  }
}


export namespace console {
  export function log(...messages: string[]): void {
    const message = messages.join("");
    const utf8 = String.UTF8.encode(message, true);
    const view = Uint8Array.wrap(utf8);
    jmLog(view.dataStart, view.length - 1);
  }
}


export function getComponent(entityId: i32, component: string): i32 {
  const utf8 = String.UTF8.encode(component, true);
  const view = Uint8Array.wrap(utf8);
  return _get_component(entityId, view.dataStart, view.length - 1);
}


export namespace __internal {
  export let dt: f32 = 0;
}

export function getDeltaTime(): f32 {
  return __internal.dt;
}




export let t: f32 = 0;

export function onUpdate(dt: f32): void {
  t += dt;
  console.log("[player] dt: " + t.toString());
  audio.playSound("thud.ogg");
}
