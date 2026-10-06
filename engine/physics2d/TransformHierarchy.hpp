#pragma once

#include <glm/glm.hpp>

#include "../core/ecs/World.hpp"
#include "../core/ecs/component/Component.hpp"
#include "TransformComponent.hpp"

// A child's place relative to its parent (see World::setParent). Its
// TransformComponent, where it is in the world, follows from this and the
// parent's every frame. Scale isn't inherited: sprites use it as their size.
struct LocalTransformComponent : Component<LocalTransformComponent> {
  COMPONENT_NAME("LocalTransformComponent");
  glm::vec3 position{0.0f};  // rotated with the parent; z adds to the parent's (draw order)
  float rotationRad = 0.0f;  // adds to the parent's
};

// Children follow their parents: registers LocalTransformComponent, keeps it
// in step with World::setParent, and registers the system that places
// children (register it after anything that moves entities in its stage).
void installTransformHierarchy(World& world);

// Where a child at `local` is in the world, given its parent's transform.
void placeChild(const TransformComponent& parent, const LocalTransformComponent& local, TransformComponent& child);
// The local transform that keeps `child` where it is under `parent`.
LocalTransformComponent localTo(const TransformComponent& parent, const TransformComponent& child);
