#pragma once

#include "../ecs/World.hpp"
#include "../ecs/entity/EntityId.hpp"
#include "../components/Transform2D.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <string_view>
#include <queue>

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
    
    // ========================================================================
    // Path Finding & Traversal
    // ========================================================================
    
    /**
     * Finds an entity by path from a root entity.
     * Path format: "child/grandchild/greatgrandchild" or "/root/child"
     * 
     * @param world - The ECS world
     * @param root - Root entity to start search from
     * @param path - Path string (e.g., "enemy/sprite" or "/player/weapon")
     * @returns EntityId if found, invalid EntityId if not found
     * 
     * Note: Currently requires entities to have name tags matching path segments.
     * This is a simplified implementation - may need enhancement based on naming system.
     */
    EntityId findByPath(World& world, EntityId root, std::string_view path);
    
    /**
     * Traverses the entity tree depth-first (pre-order).
     * Visits parent before children.
     * 
     * @param world - The ECS world
     * @param root - Root entity to start traversal from
     * @param fn - Function called for each entity: fn(EntityId)
     */
    template<typename Fn>
    void traverseDepthFirst(World& world, EntityId root, Fn&& fn);
    
    /**
     * Traverses the entity tree breadth-first (level-order).
     * Visits all entities at depth N before depth N+1.
     * 
     * @param world - The ECS world
     * @param root - Root entity to start traversal from
     * @param fn - Function called for each entity: fn(EntityId)
     */
    template<typename Fn>
    void traverseBreadthFirst(World& world, EntityId root, Fn&& fn);
}

// Template implementations (must be in header)
namespace SceneGraph {
    template<typename Fn>
    void traverseDepthFirst(World& world, EntityId root, Fn&& fn) {
        if (!world.isAlive(root)) {
            return;
        }
        
        // Visit current node
        fn(root);
        
        // Recursively visit children
        auto children = getChildren(world, root);
        for (EntityId child : children) {
            traverseDepthFirst(world, child, fn);
        }
    }
    
    template<typename Fn>
    void traverseBreadthFirst(World& world, EntityId root, Fn&& fn) {
        if (!world.isAlive(root)) {
            return;
        }
        
        std::queue<EntityId> queue;
        queue.push(root);
        
        while (!queue.empty()) {
            EntityId current = queue.front();
            queue.pop();
            
            if (!world.isAlive(current)) {
                continue;
            }
            
            // Visit current node
            fn(current);
            
            // Add children to queue
            auto children = getChildren(world, current);
            for (EntityId child : children) {
                queue.push(child);
            }
        }
    }
}
