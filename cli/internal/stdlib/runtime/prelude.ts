// ============================================================================
// Prelude - Host Function Declarations
// ============================================================================
// This file declares all external host functions that the engine provides.
// These functions are linked at runtime by the wasm3 interpreter.
//
// Function signatures must match exactly with the engine's host function
// registrations. The wasm3 signature format uses:
//   - v = void return type
//   - i = i32/u32 (both use 'i' in wasm3 signatures)
//   - f = f32 (float)
//   - I = i64/u64
//   - F = f64 (double)
//
// Note: In AssemblyScript, u32 and i32 are both represented as 'i' in wasm3
// signatures, but we use the correct AS types (u32 vs i32) for type safety.
// ============================================================================

// Logging
// Signature: v(ii) - void(int32_t ptr, int32_t len)
@external("env", "__jmLog")
declare function __jmLog(ptr: i32, len: i32): void;

// ============================================================================
// ECS Host Functions
// ============================================================================

// Entity Management
// Signature: v(ii) - void(uint32_t indexOutPtr, uint32_t generationOutPtr)
// Writes entity index and generation to memory
@external("env", "__jmCreateEntity")
declare function __jmCreateEntity(indexOutPtr: i32, generationOutPtr: i32): void;

// Signature: v(ii) - void(uint32_t index, uint32_t generation)
@external("env", "__jmDestroyEntity")
declare function __jmDestroyEntity(index: i32, generation: i32): void;

// Signature: i(ii) - int32_t(uint32_t index, uint32_t generation)
// Returns: 1 if alive, 0 if not
@external("env", "__jmIsEntityAlive")
declare function __jmIsEntityAlive(index: i32, generation: i32): i32;

// Signature: v(iiii) - void(uint32_t srcIndex, uint32_t srcGeneration, uint32_t dstIndexOutPtr, uint32_t dstGenerationOutPtr)
@external("env", "__jmCloneEntity")
declare function __jmCloneEntity(srcIndex: i32, srcGeneration: i32, dstIndexOutPtr: i32, dstGenerationOutPtr: i32): void;

// Component Operations
// Signature: i(iiii) - int32_t(uint32_t index, uint32_t generation, int32_t componentNamePtr, int32_t componentNameLen)
// Returns: 1 if component exists, 0 if not
@external("env", "__jmHasComponent")
declare function __jmHasComponent(index: i32, generation: i32, componentNamePtr: i32, componentNameLen: i32): i32;

// Signature: i(iiii) - int32_t(uint32_t index, uint32_t generation, int32_t componentNamePtr, int32_t componentNameLen)
// Returns: 1 if component exists, 0 if not (future: could return actual handle)
@external("env", "__jmGetComponentHandle")
declare function __jmGetComponentHandle(index: i32, generation: i32, componentNamePtr: i32, componentNameLen: i32): i32;

// Signature: i(iiiiii) - int32_t(uint32_t index, uint32_t generation, int32_t componentNamePtr, int32_t componentNameLen, int32_t jsonPtr, int32_t jsonLen)
// Returns: 1 on success, 0 on failure
@external("env", "__jmAddComponentFromJSON")
declare function __jmAddComponentFromJSON(index: i32, generation: i32, componentNamePtr: i32, componentNameLen: i32, jsonPtr: i32, jsonLen: i32): i32;

// Signature: v(iiii) - void(uint32_t index, uint32_t generation, int32_t componentNamePtr, int32_t componentNameLen)
@external("env", "__jmRemoveComponent")
declare function __jmRemoveComponent(index: i32, generation: i32, componentNamePtr: i32, componentNameLen: i32): void;

// Tag Operations
// Signature: v(iiii) - void(uint32_t index, uint32_t generation, int32_t tagPtr, int32_t tagLen)
@external("env", "__jmAddTag")
declare function __jmAddTag(index: i32, generation: i32, tagPtr: i32, tagLen: i32): void;

// Signature: v(iiii) - void(uint32_t index, uint32_t generation, int32_t tagPtr, int32_t tagLen)
@external("env", "__jmRemoveTag")
declare function __jmRemoveTag(index: i32, generation: i32, tagPtr: i32, tagLen: i32): void;

// Signature: i(iiii) - int32_t(uint32_t index, uint32_t generation, int32_t tagPtr, int32_t tagLen)
// Returns: 1 if has tag, 0 if not
@external("env", "__jmHasTag")
declare function __jmHasTag(index: i32, generation: i32, tagPtr: i32, tagLen: i32): i32;

// Signature: v(ii) - void(uint32_t index, uint32_t generation)
@external("env", "__jmClearTags")
declare function __jmClearTags(index: i32, generation: i32): void;

// Signature: i(iiiii) - int32_t(int32_t tagPtr, int32_t tagLen, int32_t resultIndexArrayPtr, int32_t resultGenArrayPtr, int32_t arrayCapacity)
// Returns: Number of entities found (up to arrayCapacity)
@external("env", "__jmFindEntitiesWithTag")
declare function __jmFindEntitiesWithTag(tagPtr: i32, tagLen: i32, resultIndexArrayPtr: i32, resultGenArrayPtr: i32, arrayCapacity: i32): i32;

// Audio Functions
// Signature: i(iifi) - uint32_t(int32_t ptr, int32_t len, float gain, int32_t loopFlag)
// Returns: Sound instance ID (u32) for controlling playback
@external("env", "__jmPlaySound")
declare function __jmPlaySound(ptr: i32, len: i32, gain: f32, looping: i32): u32;

// Signature: v(i) - void(uint32_t instanceId)
// Stops a playing sound instance
@external("env", "__jmStopSound")
declare function __jmStopSound(instanceId: u32): void;

// Signature: v(if) - void(uint32_t instanceId, float durationSeconds)
// Fades out a sound instance over the specified duration
@external("env", "__jmFadeOutSound")
declare function __jmFadeOutSound(instanceId: u32, durationSeconds: f32): void;

// Signature: v(if) - void(uint32_t instanceId, float gain)
// Sets the gain (volume) of a sound instance
@external("env", "__jmSetGainSound")
declare function __jmSetGainSound(instanceId: u32, gain: f32): void;