#include "TransformSystem.hpp"
#include "../ecs/World.hpp"
#include "../ecs/View.hpp"
#include <algorithm>
#include <cmath>

void TransformSystem::update(World& world, float dt) {
    (void)dt;  // Delta time not used for transform updates
    
    if (!enabled) {
        return;
    }
    
    propagateTransforms(world);
}

void TransformSystem::propagateTransforms(World& world) {
    // Step 1: Collect all entities with Transform2D component
    auto view = world.view<Transform2D>();
    
    // Step 2: Filter to only dirty transforms
    std::vector<std::pair<EntityId, Transform2D*>> dirtyTransforms;
    for (auto [entity, transform] : view) {
        if (transform->dirty) {
            dirtyTransforms.push_back({entity, transform});
        }
    }
    
    // Step 3: Sort by depth (ensures parents update before children)
    std::sort(dirtyTransforms.begin(), dirtyTransforms.end(),
        [](const auto& a, const auto& b) {
            return a.second->depth < b.second->depth;
        });
    
    // Step 4: Update each dirty transform
    for (auto [entity, transform] : dirtyTransforms) {
        updateWorldMatrix(world, *transform, entity);
        
        // Step 5: Mark all children as dirty (they need to recompute)
        for (EntityId child : transform->children) {
            if (world.isAlive(child)) {
                auto* childTransform = world.getComponent<Transform2D>(child);
                if (childTransform) {
                    childTransform->dirty = true;
                }
            }
        }
        
        // Step 6: Clear dirty flag
        transform->dirty = false;
    }
}

void TransformSystem::updateWorldMatrix(World& world, Transform2D& transform, EntityId entity) {
    (void)entity;  // Entity ID not needed for this operation
    
    // Compute local matrix from position/rotation/scale
    transform.localMatrix = computeLocalMatrix(transform);
    
    // Get parent's world matrix (or identity if no parent)
    glm::mat3 parentWorld = getParentWorldMatrix(world, transform.parent);
    
    // World = parentWorld * localMatrix
    transform.worldMatrix = parentWorld * transform.localMatrix;
}

glm::mat3 TransformSystem::computeLocalMatrix(const Transform2D& transform) const {
    // For 2D transforms with mat3, we construct the matrix manually
    // Order: Scale -> Rotate -> Translate
    
    float cosR = std::cos(transform.rotation);
    float sinR = std::sin(transform.rotation);
    
    // Construct transformation matrix:
    // [sx*cos  -sx*sin  tx]
    // [sy*sin   sy*cos  ty]
    // [  0       0       1]
    glm::mat3 matrix{
        transform.scale.x * cosR, transform.scale.y * sinR, 0.0f,
        -transform.scale.x * sinR, transform.scale.y * cosR, 0.0f,
        transform.position.x,      transform.position.y,     1.0f
    };
    
    return matrix;
}

glm::mat3 TransformSystem::getParentWorldMatrix(World& world, EntityId parent) const {
    // Invalid parent = root entity, use identity
    if (!world.isAlive(parent)) {
        return glm::mat3{1.0f};
    }
    
    auto* parentTransform = world.getComponent<Transform2D>(parent);
    if (!parentTransform) {
        // Parent exists but has no transform, use identity
        return glm::mat3{1.0f};
    }
    
    // If parent is dirty, we have a problem (shouldn't happen due to sorting)
    // For now, return parent's world matrix (may be stale, but will be updated next frame)
    return parentTransform->worldMatrix;
}
