// ============================================================================
// ECS (Entity Component System) API
// ============================================================================
// Provides comprehensive access to the Entity Component System from scripts.
// This includes entity management, component operations, and tag operations.
// ============================================================================

/**
 * EntityId represents an entity in the ECS system.
 * It contains both an index and generation for type safety.
 * 
 * Usage:
 *   let entity = Entity.create();
 *   Entity.destroy(entity);
 *   if (Entity.isAlive(entity)) {
 *     // Entity is valid
 *   }
 */
class EntityId {
  index: i32;
  generation: i32;

  constructor(index: i32, generation: i32) {
    this.index = index;
    this.generation = generation;
  }

  /**
   * Creates a new entity and returns its ID.
   */
  static create(): EntityId {
    // Allocate memory for return values (2 i32s = 8 bytes)
    const indexPtr = heap.alloc(4);
    const genPtr = heap.alloc(4);

    __jmCreateEntity(indexPtr, genPtr);

    // Read results from memory using unmanaged memory access
    const index = load<i32>(indexPtr);
    const generation = load<i32>(genPtr);

    // Free allocated memory
    heap.free(indexPtr);
    heap.free(genPtr);

    return new EntityId(index, generation);
  }
}

/**
 * Entity namespace provides entity management operations.
 */
namespace Entity {
  /**
   * Creates a new entity.
   * @returns New EntityId
   */
  export function create(): EntityId {
    return EntityId.create();
  }

  /**
   * Destroys an entity.
   * @param id - The entity to destroy
   */
  export function destroy(id: EntityId): void {
    __jmDestroyEntity(id.index, id.generation);
  }

  /**
   * Checks if an entity is alive.
   * @param id - The entity to check
   * @returns true if entity is alive, false otherwise
   */
  export function isAlive(id: EntityId): boolean {
    return __jmIsEntityAlive(id.index, id.generation) !== 0;
  }

  /**
   * Clones an entity, copying all components and tags.
   * @param id - The entity to clone
   * @returns New EntityId of the cloned entity
   */
  export function clone(id: EntityId): EntityId {
    const indexPtr = heap.alloc(4);
    const genPtr = heap.alloc(4);

    __jmCloneEntity(id.index, id.generation, indexPtr, genPtr);

    const newIndex = load<i32>(indexPtr);
    const newGeneration = load<i32>(genPtr);

    heap.free(indexPtr);
    heap.free(genPtr);

    return new EntityId(newIndex, newGeneration);
  }
}

/**
 * Component namespace provides component operations.
 */
namespace Component {
  /**
   * Checks if an entity has a component.
   * @param entityId - The entity to check
   * @param componentName - Name of the component type (e.g., "SpriteComponent")
   * @returns true if component exists, false otherwise
   */
  export function has(entityId: EntityId, componentName: string): boolean {
    const utf8 = String.UTF8.encode(componentName, true);
    const view = Uint8Array.wrap(utf8);
    return __jmHasComponent(entityId.index, entityId.generation, view.dataStart, view.length - 1) !== 0;
  }

  /**
   * Gets a component handle (currently just checks existence).
   * @param entityId - The entity
   * @param componentName - Name of the component type
   * @returns 1 if component exists, 0 if not (future: could return actual handle)
   */
  export function getHandle(entityId: EntityId, componentName: string): i32 {
    const utf8 = String.UTF8.encode(componentName, true);
    const view = Uint8Array.wrap(utf8);
    return __jmGetComponentHandle(entityId.index, entityId.generation, view.dataStart, view.length - 1);
  }

  /**
   * Adds a component to an entity from JSON data.
   * @param entityId - The entity
   * @param componentName - Name of the component type
   * @param json - JSON string containing component data
   * @returns true on success, false on failure
   */
  export function addFromJSON(entityId: EntityId, componentName: string, json: string): boolean {
    const nameUtf8 = String.UTF8.encode(componentName, true);
    const nameView = Uint8Array.wrap(nameUtf8);
    const jsonUtf8 = String.UTF8.encode(json, true);
    const jsonView = Uint8Array.wrap(jsonUtf8);

    return __jmAddComponentFromJSON(
      entityId.index,
      entityId.generation,
      nameView.dataStart,
      nameView.length - 1,
      jsonView.dataStart,
      jsonView.length - 1
    ) !== 0;
  }

  /**
   * Removes a component from an entity.
   * @param entityId - The entity
   * @param componentName - Name of the component type to remove
   */
  export function remove(entityId: EntityId, componentName: string): void {
    const utf8 = String.UTF8.encode(componentName, true);
    const view = Uint8Array.wrap(utf8);
    __jmRemoveComponent(entityId.index, entityId.generation, view.dataStart, view.length - 1);
  }
}

/**
 * Tag namespace provides tag operations for entities.
 */
namespace Tag {
  /**
   * Adds a tag to an entity.
   * @param entityId - The entity
   * @param tag - Tag name to add
   */
  export function add(entityId: EntityId, tag: string): void {
    const utf8 = String.UTF8.encode(tag, true);
    const view = Uint8Array.wrap(utf8);
    __jmAddTag(entityId.index, entityId.generation, view.dataStart, view.length - 1);
  }

  /**
   * Removes a tag from an entity.
   * @param entityId - The entity
   * @param tag - Tag name to remove
   */
  export function remove(entityId: EntityId, tag: string): void {
    const utf8 = String.UTF8.encode(tag, true);
    const view = Uint8Array.wrap(utf8);
    __jmRemoveTag(entityId.index, entityId.generation, view.dataStart, view.length - 1);
  }

  /**
   * Checks if an entity has a tag.
   * @param entityId - The entity
   * @param tag - Tag name to check
   * @returns true if entity has the tag, false otherwise
   */
  export function has(entityId: EntityId, tag: string): boolean {
    const utf8 = String.UTF8.encode(tag, true);
    const view = Uint8Array.wrap(utf8);
    return __jmHasTag(entityId.index, entityId.generation, view.dataStart, view.length - 1) !== 0;
  }

  /**
   * Removes all tags from an entity.
   * @param entityId - The entity
   */
  export function clear(entityId: EntityId): void {
    __jmClearTags(entityId.index, entityId.generation);
  }

  /**
   * Finds all entities with a specific tag.
   * @param tag - Tag name to search for
   * @param maxCount - Maximum number of entities to return
   * @returns Array of EntityIds matching the tag
   */
  export function findWithTag(tag: string, maxCount: i32): EntityId[] {
    const utf8 = String.UTF8.encode(tag, true);
    const view = Uint8Array.wrap(utf8);

    // Allocate arrays for results (4 bytes per i32)
    const indexArrayPtr = heap.alloc(4 * maxCount);
    const genArrayPtr = heap.alloc(4 * maxCount);

    const count = __jmFindEntitiesWithTag(
      view.dataStart,
      view.length - 1,
      indexArrayPtr,
      genArrayPtr,
      maxCount
    );

    // Read results into array
    const results: EntityId[] = [];
    for (let i = 0; i < count; i++) {
      const index = load<i32>(indexArrayPtr + i * 4);
      const generation = load<i32>(genArrayPtr + i * 4);
      results.push(new EntityId(index, generation));
    }

    // Free allocated memory
    heap.free(indexArrayPtr);
    heap.free(genArrayPtr);

    return results;
  }
}
