#include <gtest/gtest.h>

#include "BoxColliderComponent.hpp"
#include "CircleColliderComponent.hpp"
#include "Queries.hpp"
#include "TransformComponent.hpp"

namespace {

struct Scene {
  World world;
  Scene() {
    world.registerComponent<TransformComponent>();
    world.registerComponent<BoxColliderComponent>();
    world.registerComponent<CircleColliderComponent>();
  }
  EntityId box(float x, float y, float half, uint32_t layer = 1) {
    const EntityId id = world.createEntity();
    world.addComponent<TransformComponent>(id).position = {x, y, 0};
    auto& c = world.addComponent<BoxColliderComponent>(id);
    c.halfExtents = {half, half};
    c.layerMask = layer;
    return id;
  }
  EntityId circle(float x, float y, float radius, uint32_t layer = 1) {
    const EntityId id = world.createEntity();
    world.addComponent<TransformComponent>(id).position = {x, y, 0};
    auto& c = world.addComponent<CircleColliderComponent>(id);
    c.radius = radius;
    c.layerMask = layer;
    return id;
  }
};

}  // namespace

TEST(Queries, ARayStopsAtTheNearestColliderOnItsLayers) {
  Scene s;
  const EntityId caster = s.box(0, 0, 4);
  const EntityId far = s.box(50, 0, 4);
  const EntityId near = s.circle(20, 0, 4);
  s.box(10, 0, 2, 2);  // on another layer
  auto hit = raycast(s.world, {0, 0}, {5, 0}, 100, 1, caster);
  ASSERT_TRUE(hit);
  EXPECT_EQ(hit->entity, near);
  EXPECT_FLOAT_EQ(hit->distance, 16);
  EXPECT_EQ(hit->point, glm::vec2(16, 0));
  EXPECT_EQ(hit->normal, glm::vec2(-1, 0));
  s.world.destroyDeferred(near);
  hit = raycast(s.world, {0, 0}, {1, 0}, 100, 1, caster);
  ASSERT_TRUE(hit);
  EXPECT_EQ(hit->entity, far);
  EXPECT_FALSE(raycast(s.world, {0, 0}, {0, 0}, 100, 1, caster));  // no direction
  EXPECT_FALSE(raycast(s.world, {0, 0}, {1, 0}, 40, 1, caster));   // not that far
}

TEST(Queries, OverlapsFindEveryKindInWorldOrder) {
  Scene s;
  const EntityId a = s.box(0, 0, 5);
  const EntityId b = s.circle(12, 0, 4);
  s.box(40, 0, 5);
  s.circle(0, 0, 3, 4);  // another layer
  EXPECT_EQ(overlapping(s.world, Shape::circle({6, 0}, 3), 1), (std::vector<EntityId>{a, b}));
  EXPECT_EQ(overlapping(s.world, Shape::box({0, 0}, {0, 0}), 1), std::vector<EntityId>{a});  // a point
  EXPECT_EQ(overlapping(s.world, Shape::box({0, 0}, {0, 0}), 0xFFFFFFFFu).size(), 2u);
  EXPECT_TRUE(overlapping(s.world, Shape::box({25, 0}, {2, 2}), 1).empty());
}
