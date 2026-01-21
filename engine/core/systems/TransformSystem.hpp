#pragma once

#include "../ecs/system/System.hpp"
#include "../ecs/system/SystemTraits.hpp"
#include "../ecs/system/TypeList.hpp"
#include "../components/Transform2D.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Tag type for systems that depend on transform updates
struct TransformUpdateTag {};

class TransformSystem : public System {
public:
    TransformSystem() = default;
    ~TransformSystem() = default;
    
    void update(World& world, float dt) override;
    
    const char* name() const override { return "TransformSystem"; }
    
private:
    // Main propagation algorithm
    void propagateTransforms(World& world);
    
    // Update a single transform's world matrix
    void updateWorldMatrix(World& world, Transform2D& transform, EntityId entity);
    
    // Compute local matrix from position/rotation/scale
    glm::mat3 computeLocalMatrix(const Transform2D& transform) const;
    
    // Get parent's world matrix (or identity if no parent)
    glm::mat3 getParentWorldMatrix(World& world, EntityId parent) const;
};

// SystemTraits specialization must come after class definition
template<>
struct SystemTraits<TransformSystem> {
    using Provides = TypeList<TransformUpdateTag>;
    using Writes = TypeList<Transform2D>;
    using Reads = TypeList<>;
    using DependsOn = TypeList<>;  // Runs early, no dependencies
};
