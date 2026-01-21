// ============================================================================
// ECS (Entity Component System) API
// ============================================================================
// Provides access to entity components from scripts.
// Components are accessed by name and entity ID.
// ============================================================================

/**
 * Gets a component handle for an entity.
 * 
 * @param entityId - The ID of the entity
 * @param component - The name of the component type (e.g., "SpriteComponent")
 * @returns Component handle/pointer (i32), or 0 if the component doesn't exist
 * 
 * Usage:
 *   let spriteHandle = getComponent(entityId, "SpriteComponent");
 *   if (spriteHandle !== 0) {
 *     // Component exists, can be used
 *   }
 * 
 * Note: The returned handle is an opaque pointer. Actual component access
 * is typically handled through higher-level APIs or additional host functions.
 */
function getComponent(entityId: i32, component: string): i32 {
  const utf8 = String.UTF8.encode(component, true);
  const view = Uint8Array.wrap(utf8);
  return _get_component(entityId, view.dataStart, view.length - 1);
}
