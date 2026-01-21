#include "SceneGraph.hpp"
#include "../ecs/World.hpp"
#include <stdexcept>
#include <algorithm>
#include <functional>
#include <string>
#include <string_view>

namespace {
    // Internal helper to recursively update depths
    void updateDepthsRecursive(World& world, EntityId entity, int32_t newDepth) {
        auto* transform = world.getComponent<Transform2D>(entity);
        if (!transform) return;
        
        transform->depth = newDepth;
        for (EntityId child : transform->children) {
            if (world.isAlive(child)) {
                updateDepthsRecursive(world, child, newDepth + 1);
            }
        }
    }
    
    // Internal helper to recursively mark dirty
    void markDirtyRecursive(World& world, EntityId entity) {
        auto* transform = world.getComponent<Transform2D>(entity);
        if (!transform) return;
        
        transform->dirty = true;
        for (EntityId child : transform->children) {
            if (world.isAlive(child)) {
                markDirtyRecursive(world, child);
            }
        }
    }
}

void SceneGraph::setParent(World& world, EntityId child, EntityId parent) {
    // Validate entities are alive
    if (!world.isAlive(child)) {
        throw std::runtime_error("Cannot set parent: child entity is not alive");
    }
    
    // Get child's transform (must exist)
    auto* childTransform = world.getComponent<Transform2D>(child);
    if (!childTransform) {
        throw std::runtime_error("Cannot set parent: child entity has no Transform2D component");
    }
    
    // If parent is invalid, make root
    if (!world.isAlive(parent) || parent.index == 0) {
        removeFromParent(world, child);
        return;
    }
    
    // Validate parent has Transform2D
    auto* parentTransform = world.getComponent<Transform2D>(parent);
    if (!parentTransform) {
        throw std::runtime_error("Cannot set parent: parent entity has no Transform2D component");
    }
    
    // Check for circular dependency
    EntityId current = parent;
    while (world.isAlive(current)) {
        if (current == child) {
            throw std::runtime_error("Cannot set parent: circular dependency detected");
        }
        auto* currentTransform = world.getComponent<Transform2D>(current);
        if (!currentTransform || !world.isAlive(currentTransform->parent)) {
            break;
        }
        current = currentTransform->parent;
    }
    
    // Remove child from old parent's children list
    EntityId oldParent = childTransform->parent;
    if (world.isAlive(oldParent)) {
        auto* oldParentTransform = world.getComponent<Transform2D>(oldParent);
        if (oldParentTransform) {
            auto& children = oldParentTransform->children;
            children.erase(
                std::remove(children.begin(), children.end(), child),
                children.end()
            );
        }
    }
    
    // Add child to new parent's children list
    parentTransform->children.push_back(child);
    
    // Update child's parent and depth
    childTransform->parent = parent;
    childTransform->depth = parentTransform->depth + 1;
    
    // Recursively update depths of all descendants
    updateDepthsRecursive(world, child, childTransform->depth);
    
    // Mark child and all descendants as dirty
    markDirtyRecursive(world, child);
}

void SceneGraph::removeFromParent(World& world, EntityId entity) {
    auto* transform = world.getComponent<Transform2D>(entity);
    if (!transform) return;
    
    EntityId parent = transform->parent;
    if (!world.isAlive(parent)) {
        return;  // Already root
    }
    
    // Remove from parent's children list
    auto* parentTransform = world.getComponent<Transform2D>(parent);
    if (parentTransform) {
        auto& children = parentTransform->children;
        children.erase(
            std::remove(children.begin(), children.end(), entity),
            children.end()
        );
    }
    
    // Make root
    transform->parent = EntityId{0, 0};
    transform->depth = 0;
    
    // Update depths of descendants
    updateDepthsRecursive(world, entity, 0);
    
    // Mark dirty
    markDirtyRecursive(world, entity);
}

EntityId SceneGraph::getParent(World& world, EntityId entity) {
    auto* transform = world.getComponent<Transform2D>(entity);
    if (!transform) return EntityId{0, 0};
    return transform->parent;
}

std::vector<EntityId> SceneGraph::getChildren(World& world, EntityId entity) {
    auto* transform = world.getComponent<Transform2D>(entity);
    if (!transform) return {};
    return transform->children;  // Return copy
}

std::vector<EntityId> SceneGraph::getAncestors(World& world, EntityId entity) {
    std::vector<EntityId> ancestors;
    EntityId current = entity;
    
    while (world.isAlive(current)) {
        auto* transform = world.getComponent<Transform2D>(current);
        if (!transform) break;
        
        EntityId parent = transform->parent;
        if (!world.isAlive(parent)) break;
        
        ancestors.push_back(parent);
        current = parent;
    }
    
    return ancestors;
}

std::vector<EntityId> SceneGraph::getDescendants(World& world, EntityId entity) {
    std::vector<EntityId> descendants;
    
    std::function<void(EntityId)> collect = [&](EntityId e) {
        auto* transform = world.getComponent<Transform2D>(e);
        if (!transform) return;
        
        for (EntityId child : transform->children) {
            if (world.isAlive(child)) {
                descendants.push_back(child);
                collect(child);
            }
        }
    };
    
    collect(entity);
    return descendants;
}

void SceneGraph::destroyRecursive(World& world, EntityId entity) {
    auto* transform = world.getComponent<Transform2D>(entity);
    if (transform) {
        // Destroy all children first (recursive)
        for (EntityId child : transform->children) {
            if (world.isAlive(child)) {
                destroyRecursive(world, child);
            }
        }
    }
    
    // Remove from parent before destroying
    removeFromParent(world, entity);
    
    // Destroy entity
    world.destroyEntity(entity);
}

EntityId SceneGraph::createChild(World& world, EntityId parent) {
    if (!world.isAlive(parent)) {
        throw std::runtime_error("Cannot create child: parent entity is not alive");
    }
    
    auto* parentTransform = world.getComponent<Transform2D>(parent);
    if (!parentTransform) {
        throw std::runtime_error("Cannot create child: parent entity has no Transform2D component");
    }
    
    EntityId child = world.createEntity();
    auto& childTransform = world.addComponent<Transform2D>(child);
    
    // Set parent
    childTransform.parent = parent;
    childTransform.depth = parentTransform->depth + 1;
    childTransform.dirty = true;
    
    // Add to parent's children
    parentTransform->children.push_back(child);
    
    return child;
}

glm::vec2 SceneGraph::getWorldPosition(World& world, EntityId entity) {
    auto* transform = world.getComponent<Transform2D>(entity);
    if (!transform) return {0.0f, 0.0f};
    
    // Extract translation from world matrix
    return {transform->worldMatrix[2][0], transform->worldMatrix[2][1]};
}

void SceneGraph::setWorldPosition(World& world, EntityId entity, glm::vec2 worldPos) {
    auto* transform = world.getComponent<Transform2D>(entity);
    if (!transform) return;
    
    // Get parent's world matrix
    glm::mat3 parentWorld{1.0f};
    if (world.isAlive(transform->parent)) {
        auto* parentTransform = world.getComponent<Transform2D>(transform->parent);
        if (parentTransform) {
            parentWorld = parentTransform->worldMatrix;
        }
    }
    
    // Compute inverse parent transform
    glm::mat3 invParent = glm::inverse(parentWorld);
    
    // Transform world position to local
    glm::vec3 worldPos3 = {worldPos.x, worldPos.y, 1.0f};
    glm::vec3 localPos3 = invParent * worldPos3;
    
    // Update local position
    transform->position = {localPos3.x, localPos3.y};
    transform->dirty = true;
}

glm::vec2 SceneGraph::localToWorld(World& world, EntityId entity, glm::vec2 localPos) {
    auto* transform = world.getComponent<Transform2D>(entity);
    if (!transform) return localPos;
    
    glm::vec3 localPos3 = {localPos.x, localPos.y, 1.0f};
    glm::vec3 worldPos3 = transform->worldMatrix * localPos3;
    return {worldPos3.x, worldPos3.y};
}

glm::vec2 SceneGraph::worldToLocal(World& world, EntityId entity, glm::vec2 worldPos) {
    auto* transform = world.getComponent<Transform2D>(entity);
    if (!transform) return worldPos;
    
    glm::mat3 invWorld = glm::inverse(transform->worldMatrix);
    glm::vec3 worldPos3 = {worldPos.x, worldPos.y, 1.0f};
    glm::vec3 localPos3 = invWorld * worldPos3;
    return {localPos3.x, localPos3.y};
}

EntityId SceneGraph::findByPath(World& world, EntityId root, std::string_view path) {
    if (!world.isAlive(root)) {
        return EntityId{0, 0};
    }
    
    // Handle absolute path (starts with '/')
    std::string_view searchPath = path;
    if (!searchPath.empty() && searchPath[0] == '/') {
        searchPath = searchPath.substr(1);  // Remove leading '/'
    }
    
    // Split path by '/'
    EntityId current = root;
    size_t start = 0;
    
    while (start < searchPath.length()) {
        size_t end = searchPath.find('/', start);
        if (end == std::string_view::npos) {
            end = searchPath.length();
        }
        
        std::string_view segment = searchPath.substr(start, end - start);
        if (segment.empty()) {
            start = end + 1;
            continue;
        }
        
        // Find child with matching name (via tag)
        // Note: This assumes entities have name tags matching the segment
        bool found = false;
        auto children = getChildren(world, current);
        std::string segmentStr(segment);
        
        for (EntityId child : children) {
            if (!world.isAlive(child)) {
                continue;
            }
            
            // Check if entity has a tag matching the segment
            if (world.hasTag(child, segmentStr)) {
                current = child;
                found = true;
                break;
            }
        }
        
        if (!found) {
            return EntityId{0, 0};  // Path not found
        }
        
        start = end + 1;
    }
    
    return current;
}
