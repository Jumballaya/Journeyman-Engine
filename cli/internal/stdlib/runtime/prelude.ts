@external("env", "__jmLog")
declare function __jmLog(ptr: i32, len: i32): void;

@external("env", "__jmEcsGetComponent")
declare function __jmEcsGetComponent(namePtr: i32, nameLen: i32, outPtr: i32, outLen: i32): i32;

@external("env", "__jmEcsUpdateComponent")
declare function __jmEcsUpdateComponent(namePtr: i32, nameLen: i32, dataPtr: i32): i32;

@external("env", "__jmPlaySound")
declare function __jmPlaySound(ptr: i32, len: i32, gain: f32, looping: i32): u32;

@external("env", "__jmStopSound")
declare function __jmStopSound(ptr: i32): void;

@external("env", "__jmFadeOutSound")
declare function __jmFadeOutSound(ptr: i32, durationSeconds: f32): void;

@external("env", "__jmSetGainSound")
declare function __jmSetGainSound(ptr: i32, gain: f32): void;

@external("env", "__jmKeyIsPressed")
declare function __jmKeyIsPressed(key: i32): i32;

@external("env", "__jmKeyIsReleased")
declare function __jmKeyIsReleased(key: i32): i32;

@external("env", "__jmKeyIsDown")
declare function __jmKeyIsDown(key: i32): i32;