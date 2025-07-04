@external("env", "jmLog")
declare function jmLog(ptr: i32, len: i32): void;

@external("env", "get_component")
declare function _get_component(entityId: i32, typePtr: i32, typeLen: i32): i32;

@external("env", "__jmPlaySound")
declare function __jmPlaySound(ptr: i32, len: i32, gain: f32, looping: i32): u32;

@external("env", "__jmStopSound")
declare function __jmStopSound(ptr: i32): void;

@external("env", "__jmFadeOutSound")
declare function __jmFadeOutSound(ptr: i32, durationSeconds: f32): void;

@external("env", "__jmSetGainSound")
declare function __jmSetGainSound(ptr: i32, gain: f32): void;