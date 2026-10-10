#pragma once

#include "../core/ecs/World.hpp"
#include "BoxColliderComponent.hpp"
#include "CircleColliderComponent.hpp"
#include "Shapes.hpp"
#include "TransformComponent.hpp"

// A collider as physics sees it: whose, its shape where it is now, its layers.
struct Collider {
  EntityId entity;
  Shape shape;
  uint32_t layerMask, collidesWithMask;
};

// Calls visit(const Collider&) for every collider in the world, in one order
// the collision system and queries share: boxes, then circles, each in world
// order. Entities about to be destroyed have none.
template <typename Visit>
void forEachCollider(World& world, Visit visit) {
  for (auto [entity, trans, box] : world.view<TransformComponent, BoxColliderComponent>()) {
    if (world.isPendingDestroy(entity)) continue;
    visit(Collider{entity, Shape::box(glm::vec2(trans->position) + box->offset, box->halfExtents), box->layerMask,
                   box->collidesWithMask});
  }
  for (auto [entity, trans, circle] : world.view<TransformComponent, CircleColliderComponent>()) {
    if (world.isPendingDestroy(entity)) continue;
    visit(Collider{entity, Shape::circle(glm::vec2(trans->position) + circle->offset, circle->radius), circle->layerMask,
                   circle->collidesWithMask});
  }
}
