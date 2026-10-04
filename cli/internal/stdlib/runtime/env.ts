// Host functions the engine links into every script (module "env").
// Strings are passed as (ptr, len) UTF-8; entities as (index, generation);
// entity results as i64 (generation << 32 | index, -1 = none). Internal.

export declare function __jmLog(ptr: usize, len: i32): void;

export declare function __jmSelf(): i64;
export declare function __jmEntityIsAlive(index: u32, generation: u32): bool;
export declare function __jmEntityHasTag(index: u32, generation: u32, ptr: usize, len: i32): bool;
export declare function __jmEntitySetTag(index: u32, generation: u32, ptr: usize, len: i32, present: bool): void;
export declare function __jmEntityHasComponent(index: u32, generation: u32, ptr: usize, len: i32): bool;
export declare function __jmWorldDestroy(index: u32, generation: u32): void;
export declare function __jmWorldFindFirst(ptr: usize, len: i32): i64;
export declare function __jmWorldFindAll(ptr: usize, len: i32, out: usize, outBytes: i32): i32;
export declare function __jmWorldSpawn(prefab: usize, prefabLen: i32, x: f32, y: f32, overrides: usize, overridesLen: i32): i64;

export declare function __jmFieldId(component: usize, componentLen: i32, field: usize, fieldLen: i32): i32;
export declare function __jmFieldGet(index: u32, generation: u32, field: i32): u32;
export declare function __jmFieldSet(index: u32, generation: u32, field: i32, bits: u32): void;

export declare function __jmParamNumber(ptr: usize, len: i32, fallback: f64): f64;
export declare function __jmParamString(ptr: usize, len: i32, out: usize, cap: i32): i32;

export declare function __jmTimeScale(): f32;
export declare function __jmTimeSetScale(scale: f32): void;
export declare function __jmTimeElapsed(): f64;
export declare function __jmTimeUnscaledElapsed(): f64;
export declare function __jmTimeUnscaledDelta(): f32;

export declare function __jmStateGetNumber(store: i32, ptr: usize, len: i32, fallback: f64): f64;
export declare function __jmStateSetNumber(store: i32, ptr: usize, len: i32, value: f64): void;
export declare function __jmStateGetString(store: i32, ptr: usize, len: i32, out: usize, cap: i32): i32;
export declare function __jmStateSetString(store: i32, ptr: usize, len: i32, value: usize, valueLen: i32): void;
export declare function __jmStateHas(store: i32, ptr: usize, len: i32): bool;
export declare function __jmStateRemove(store: i32, ptr: usize, len: i32): void;
export declare function __jmStateClear(store: i32): void;

export declare function __jmSceneLoad(ptr: usize, len: i32): void;
export declare function __jmSceneTransition(ptr: usize, len: i32, seconds: f32, shader: usize, shaderLen: i32): void;
export declare function __jmSceneIsTransitioning(): bool;
export declare function __jmSceneCurrent(out: usize, cap: i32): i32;

export declare function __jmAppQuit(): void;

export declare function __jmKeyState(key: i32, query: i32): bool;
export declare function __jmActionState(ptr: usize, len: i32, query: i32): bool;
export declare function __jmActionValue(ptr: usize, len: i32): f32;
export declare function __jmActionBind(action: usize, actionLen: i32, control: usize, controlLen: i32): bool;
export declare function __jmActionUnbind(ptr: usize, len: i32): void;
export declare function __jmGamepadConnected(): bool;

export declare function __jmSoundPlay(ptr: usize, len: i32, gain: f32, loop: bool, bus: i32): u32;
export declare function __jmSoundStop(id: u32): void;
export declare function __jmSoundFadeOut(id: u32, seconds: f32): void;
export declare function __jmSoundSetGain(id: u32, gain: f32): void;
export declare function __jmAudioSetBusVolume(bus: i32, volume: f32): void;
export declare function __jmAudioStopAll(fadeSeconds: f32): void;

export declare function __jmUISetText(id: usize, idLen: i32, text: usize, textLen: i32): bool;
export declare function __jmUISetClass(id: usize, idLen: i32, cls: usize, clsLen: i32, on: bool): bool;
export declare function __jmUISetStyle(id: usize, idLen: i32, prop: usize, propLen: i32, value: usize, valueLen: i32): bool;
export declare function __jmUISetAttribute(id: usize, idLen: i32, name: usize, nameLen: i32, value: usize, valueLen: i32): bool;
export declare function __jmUIExists(id: usize, idLen: i32): bool;

export declare function __jmEffectAddBuiltin(ptr: usize, len: i32): u32;
export declare function __jmEffectAddCustom(ptr: usize, len: i32): u32;
export declare function __jmEffectRemove(id: u32): void;
export declare function __jmEffectSetEnabled(id: u32, on: bool): void;
export declare function __jmEffectSetUniform(id: u32, ptr: usize, len: i32, count: i32, x: f32, y: f32, z: f32, w: f32): void;
export declare function __jmCameraShake(amplitude: f32, seconds: f32): void;
export declare function __jmCameraSetPosition(x: f32, y: f32): void;
export declare function __jmRendererSetClearColor(r: f32, g: f32, b: f32, a: f32): void;
export declare function __jmSpritePlay(index: u32, generation: u32, ptr: usize, len: i32): bool;
export declare function __jmSpriteFinished(index: u32, generation: u32): bool;

export declare function __jmWindowSetFullscreen(on: bool): void;
export declare function __jmWindowIsFullscreen(): bool;
export declare function __jmWindowIsFocused(): bool;
