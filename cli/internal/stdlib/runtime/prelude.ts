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

// ECS Component Access
// Signature: i(iii) - int32_t(int32_t entityId, int32_t typePtr, int32_t typeLen)
// Returns: Component handle/pointer (i32) or 0 if not found
@external("env", "get_component")
declare function _get_component(entityId: i32, typePtr: i32, typeLen: i32): i32;

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