@external("env", "__jmLog")
export declare function __jmLog(ptr: i32, len: i32): void;

@external("env", "__jmEcsGetComponent")
export declare function __jmEcsGetComponent(namePtr: i32, nameLen: i32, outPtr: i32, outLen: i32): i32;

@external("env", "__jmEcsUpdateComponent")
export declare function __jmEcsUpdateComponent(namePtr: i32, nameLen: i32, dataPtr: i32): i32;

@external("env", "__jmPlaySound")
export declare function __jmPlaySound(ptr: i32, len: i32, gain: f32, looping: i32): u32;

@external("env", "__jmStopSound")
export declare function __jmStopSound(ptr: i32): void;

@external("env", "__jmFadeOutSound")
export declare function __jmFadeOutSound(ptr: i32, durationSeconds: f32): void;

@external("env", "__jmSetGainSound")
export declare function __jmSetGainSound(ptr: i32, gain: f32): void;

@external("env", "__jmKeyIsPressed")
export declare function __jmKeyIsPressed(key: i32): i32;

@external("env", "__jmKeyIsReleased")
export declare function __jmKeyIsReleased(key: i32): i32;

@external("env", "__jmKeyIsDown")
export declare function __jmKeyIsDown(key: i32): i32;

@external("env", "__jmRendererAddBuiltin")
export declare function __jmRendererAddBuiltin(builtinId: i32): i32;

@external("env", "__jmRendererRemoveEffect")
export declare function __jmRendererRemoveEffect(handleId: i32): void;

@external("env", "__jmRendererSetEffectEnabled")
export declare function __jmRendererSetEffectEnabled(handleId: i32, enabled: i32): void;

@external("env", "__jmRendererSetEffectUniformFloat")
export declare function __jmRendererSetEffectUniformFloat(handleId: i32, namePtr: i32, nameLen: i32, value: f32): void;

@external("env", "__jmRendererSetEffectUniformVec3")
export declare function __jmRendererSetEffectUniformVec3(handleId: i32, namePtr: i32, nameLen: i32, x: f32, y: f32, z: f32): void;

@external("env", "__jmRendererEffectCount")
export declare function __jmRendererEffectCount(): i32;

@external("env", "__jmSceneLoad")
export declare function __jmSceneLoad(namePtr: i32, nameLen: i32): void;

@external("env", "__jmSceneTransition")
export declare function __jmSceneTransition(namePtr: i32, nameLen: i32, durationSeconds: f32): void;

@external("env", "__jmSceneIsTransitioning")
export declare function __jmSceneIsTransitioning(): i32;
