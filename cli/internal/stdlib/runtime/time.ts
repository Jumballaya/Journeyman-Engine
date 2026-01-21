// ============================================================================
// Time API
// ============================================================================
// Provides time-related utilities for scripts.
// ============================================================================

// Internal namespace for storing time state
// The dt value is set by the engine before calling onUpdate()
namespace __internal {
  let dt: f32 = 0;
}

/**
 * Gets the delta time (time since last frame) in seconds.
 * 
 * This value is automatically updated by the engine before each onUpdate() call.
 * 
 * @returns Delta time in seconds (typically 0.016 for 60 FPS)
 * 
 * Usage:
 *   export function onUpdate(dt: f32): void {
 *     let actualDt = getDeltaTime();  // Same as the dt parameter
 *     // Use delta time for frame-rate independent movement
 *     position += velocity * actualDt;
 *   }
 */
function getDeltaTime(): f32 {
  return __internal.dt;
}
