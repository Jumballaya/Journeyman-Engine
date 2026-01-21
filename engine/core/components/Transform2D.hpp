#pragma once

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../ecs/component/Component.hpp"
#include "../ecs/entity/EntityId.hpp"

struct Transform2D : public Component<Transform2D> {
    COMPONENT_NAME("Transform2D");
    
    // Local transform (relative to parent)
    glm::vec2 position{0.0f, 0.0f};
    float rotation{0.0f};           // radians, counter-clockwise
    glm::vec2 scale{1.0f, 1.0f};
    
    // Cached world transform matrices
    glm::mat3 localMatrix{1.0f};    // Computed from position/rotation/scale
    glm::mat3 worldMatrix{1.0f};    // parentWorld * localMatrix
    
    // Hierarchy relationships
    EntityId parent{0, 0};          // Invalid EntityId = root (no parent)
    std::vector<EntityId> children; // Direct children only
    
    // Dirty tracking for efficient updates
    bool dirty{true};               // true = needs world matrix recomputation
    int32_t depth{0};               // 0 = root, increases with each level
    
    // Default constructor
    Transform2D() = default;
    
    // Convenience constructor
    Transform2D(glm::vec2 pos, float rot = 0.0f, glm::vec2 scl = {1.0f, 1.0f})
        : position(pos), rotation(rot), scale(scl), dirty(true) {}
};
