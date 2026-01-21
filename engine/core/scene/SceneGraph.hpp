#pragma once

#include "../ecs/World.hpp"
#include "../ecs/entity/EntityId.hpp"
#include "../components/Transform2D.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace SceneGraph {
    // ========================================================================
    // Hierarchy Manipulation
    // ========================================================================
    
    /**
     * Sets the parent of an entity.
     * If parent is invalid EntityId, makes entity a root (removes from current parent).
     * 
     * @param world - The ECS world
     * @param child - Entity to reparent
     * @param parent - New parent entity (or invalid EntityId for root)
     * 
     * @throws std::runtime_error if circular dependency detected
     */
    void setParent(World& world, EntityId child, EntityId parent);
    
    /**
     * Removes an entity from its parent, making it a root.
     * Equivalent to setParent(world, entity, invalidEntityId).
     */
    void removeFromParent(World& world, EntityId entity);
    
    /**
     * Gets the parent of an entity.
     * @returns Parent EntityId, or invalid EntityId if root
     */
    EntityId getParent(World& world, EntityId entity);
    
    /**
     * Gets all direct children of an entity.
     * @returns Vector of child EntityIds (empty if none)
     */
    std::vector<EntityId> getChildren(World& world, EntityId entity);
    
    /**
     * Gets all ancestor entities (parent, grandparent, etc.) up to root.
     * @returns Vector of ancestor EntityIds, ordered from parent to root
     */
    std::vector<EntityId> getAncestors(World& world, EntityId entity);
    
    /**
     * Gets all descendant entities (children, grandchildren, etc.).
     * @returns Vector of all descendant EntityIds
     */
    std::vector<EntityId> getDescendants(World& world, EntityId entity);
    
    /**
     * Destroys an entity and all its descendants recursively.
     * Useful for destroying entire subtrees.
     */
    void destroyRecursive(World& world, EntityId entity);
    
    /**
     * Creates a child entity under a parent.
     * @returns The created child entity
     */
    EntityId createChild(World& world, EntityId parent);
    
    // ========================================================================
    // Transform Utilities
    // ========================================================================
    
    /**
     * Gets the world position of an entity (extracted from world matrix).
     */
    glm::vec2 getWorldPosition(World& world, EntityId entity);
    
    /**
     * Sets the world position of an entity (updates local position relative to parent).
     */
    void setWorldPosition(World& world, EntityId entity, glm::vec2 worldPos);
    
    /**
     * Converts a local position to world coordinates.
     */
    glm::vec2 localToWorld(World& world, EntityId entity, glm::vec2 localPos);
    
    /**
     * Converts a world position to local coordinates.
     */
    glm::vec2 worldToLocal(World& world, EntityId entity, glm::vec2 worldPos);
}
