@external("env", "__jmLog")
export declare function __jmLog(ptr: i32, len: i32): void;

@external("env", "__jmEcsGetComponent")
export declare function __jmEcsGetComponent(namePtr: i32, nameLen: i32, outPtr: i32, outLen: i32): i32;

@external("env", "__jmEcsUpdateComponent")
export declare function __jmEcsUpdateComponent(namePtr: i32, nameLen: i32, dataPtr: i32): i32;

@external("env", "__jmPlaySound")
export declare function __jmPlaySound(ptr: i32, len: i32, gain: f32, looping: i32, bus: i32): u32;

@external("env", "__jmAudioSetBusVolume")
export declare function __jmAudioSetBusVolume(bus: i32, volume: f32): void;

@external("env", "__jmAudioStopAll")
export declare function __jmAudioStopAll(fadeSeconds: f32): void;

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

@external("env", "__jmSpriteSetAnimation")
export declare function __jmSpriteSetAnimation(entityIndex: i32, entityGeneration: i32, namePtr: i32, nameLen: i32): void;

@external("env", "__jmSpriteIsAnimationFinished")
export declare function __jmSpriteIsAnimationFinished(entityIndex: i32, entityGeneration: i32): i32;

@external("env", "__jmEcsGetComponentOf")
export declare function __jmEcsGetComponentOf(index: i32, generation: i32, namePtr: i32, nameLen: i32, outPtr: i32, outLen: i32): i32;

@external("env", "__jmEcsUpdateComponentOf")
export declare function __jmEcsUpdateComponentOf(index: i32, generation: i32, namePtr: i32, nameLen: i32, dataPtr: i32): i32;

@external("env", "__jmSelf")
export declare function __jmSelf(): i64;

@external("env", "__jmEntityIsAlive")
export declare function __jmEntityIsAlive(index: i32, generation: i32): i32;

@external("env", "__jmEntityHasTag")
export declare function __jmEntityHasTag(index: i32, generation: i32, tagPtr: i32, tagLen: i32): i32;

@external("env", "__jmEntitySetTag")
export declare function __jmEntitySetTag(index: i32, generation: i32, tagPtr: i32, tagLen: i32, present: i32): void;

@external("env", "__jmWorldFindFirst")
export declare function __jmWorldFindFirst(tagPtr: i32, tagLen: i32): i64;

@external("env", "__jmWorldFindAll")
export declare function __jmWorldFindAll(tagPtr: i32, tagLen: i32, outPtr: i32, capacity: i32): i32;

@external("env", "__jmWorldSpawn")
export declare function __jmWorldSpawn(pathPtr: i32, pathLen: i32, x: f32, y: f32, overridesPtr: i32, overridesLen: i32): i64;

@external("env", "__jmWorldDestroy")
export declare function __jmWorldDestroy(index: i32, generation: i32): void;

@external("env", "__jmScriptParamNumber")
export declare function __jmScriptParamNumber(keyPtr: i32, keyLen: i32, fallback: f64): f64;

@external("env", "__jmScriptParamString")
export declare function __jmScriptParamString(keyPtr: i32, keyLen: i32, outPtr: i32, capacity: i32): i32;

@external("env", "__jmTimeScale")
export declare function __jmTimeScale(): f32;

@external("env", "__jmTimeSetScale")
export declare function __jmTimeSetScale(scale: f32): void;

@external("env", "__jmTimeElapsed")
export declare function __jmTimeElapsed(): f64;

@external("env", "__jmTimeUnscaledElapsed")
export declare function __jmTimeUnscaledElapsed(): f64;

@external("env", "__jmTimeUnscaledDelta")
export declare function __jmTimeUnscaledDelta(): f32;

@external("env", "__jmStateSetNumber")
export declare function __jmStateSetNumber(store: i32, keyPtr: i32, keyLen: i32, value: f64): void;

@external("env", "__jmStateGetNumber")
export declare function __jmStateGetNumber(store: i32, keyPtr: i32, keyLen: i32, fallback: f64): f64;

@external("env", "__jmStateSetString")
export declare function __jmStateSetString(store: i32, keyPtr: i32, keyLen: i32, valuePtr: i32, valueLen: i32): void;

@external("env", "__jmStateGetString")
export declare function __jmStateGetString(store: i32, keyPtr: i32, keyLen: i32, outPtr: i32, capacity: i32): i32;

@external("env", "__jmStateHas")
export declare function __jmStateHas(store: i32, keyPtr: i32, keyLen: i32): i32;

@external("env", "__jmStateRemove")
export declare function __jmStateRemove(store: i32, keyPtr: i32, keyLen: i32): void;

@external("env", "__jmStateClear")
export declare function __jmStateClear(store: i32): void;

@external("env", "__jmAppQuit")
export declare function __jmAppQuit(): void;

@external("env", "__jmActionState")
export declare function __jmActionState(namePtr: i32, nameLen: i32, query: i32): i32;

@external("env", "__jmActionValue")
export declare function __jmActionValue(namePtr: i32, nameLen: i32): f32;

@external("env", "__jmActionBind")
export declare function __jmActionBind(namePtr: i32, nameLen: i32, controlPtr: i32, controlLen: i32): i32;

@external("env", "__jmActionUnbind")
export declare function __jmActionUnbind(namePtr: i32, nameLen: i32): void;

@external("env", "__jmGamepadConnected")
export declare function __jmGamepadConnected(): i32;

@external("env", "__jmRendererAddCustom")
export declare function __jmRendererAddCustom(pathPtr: i32, pathLen: i32): i32;

@external("env", "__jmRendererSetEffectUniformVec4")
export declare function __jmRendererSetEffectUniformVec4(handleId: i32, namePtr: i32, nameLen: i32, x: f32, y: f32, z: f32, w: f32): void;

@external("env", "__jmCameraShake")
export declare function __jmCameraShake(amplitude: f32, duration: f32): void;

@external("env", "__jmCameraSetPosition")
export declare function __jmCameraSetPosition(x: f32, y: f32): void;

@external("env", "__jmRendererSetClearColor")
export declare function __jmRendererSetClearColor(r: f32, g: f32, b: f32, a: f32): void;

@external("env", "__jmWindowSetFullscreen")
export declare function __jmWindowSetFullscreen(on: i32): void;

@external("env", "__jmWindowIsFullscreen")
export declare function __jmWindowIsFullscreen(): i32;

@external("env", "__jmSceneTransitionWith")
export declare function __jmSceneTransitionWith(namePtr: i32, nameLen: i32, durationSeconds: f32, shaderPtr: i32, shaderLen: i32): void;

@external("env", "__jmSceneCurrent")
export declare function __jmSceneCurrent(outPtr: i32, capacity: i32): i32;

@external("env", "__jmUISetText")
export declare function __jmUISetText(idPtr: i32, idLen: i32, textPtr: i32, textLen: i32): i32;

@external("env", "__jmUISetClass")
export declare function __jmUISetClass(idPtr: i32, idLen: i32, clsPtr: i32, clsLen: i32, on: i32): i32;

@external("env", "__jmUISetStyle")
export declare function __jmUISetStyle(idPtr: i32, idLen: i32, propPtr: i32, propLen: i32, valuePtr: i32, valueLen: i32): i32;

@external("env", "__jmUISetAttribute")
export declare function __jmUISetAttribute(idPtr: i32, idLen: i32, namePtr: i32, nameLen: i32, valuePtr: i32, valueLen: i32): i32;

@external("env", "__jmUIExists")
export declare function __jmUIExists(idPtr: i32, idLen: i32): i32;
