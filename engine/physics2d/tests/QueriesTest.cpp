#include <gtest/gtest.h>

#include "BoxColliderComponent.hpp"
#include "CircleColliderComponent.hpp"
#include "Queries.hpp"
#include "Terrain.hpp"
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
    c.collisionLayer = layer;
    return id;
  }
  EntityId circle(float x, float y, float radius, uint32_t layer = 1) {
    const EntityId id = world.createEntity();
    world.addComponent<TransformComponent>(id).position = {x, y, 0};
    auto& c = world.addComponent<CircleColliderComponent>(id);
    c.radius = radius;
    c.collisionLayer = layer;
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

TEST(Queries, AnyDirectionLengthAndNoLimit) {
  Scene s;
  const EntityId far = s.box(100, 0, 2);
  for (const glm::vec2 direction : {glm::vec2(1e-30f, 0), glm::vec2(1e30f, 0), glm::vec2(3, 0)}) {
    auto hit = raycast(s.world, {0, 0}, direction, INFINITY, 1);
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit->entity, far);
    EXPECT_FLOAT_EQ(hit->distance, 98);
  }
  EXPECT_FALSE(raycast(s.world, {NAN, 0}, {1, 0}, 100, 1));
  EXPECT_FALSE(raycast(s.world, {0, 0}, {1, NAN}, 100, 1));
  EXPECT_FALSE(raycast(s.world, {0, 0}, {INFINITY, 0}, 100, 1));
  EXPECT_FALSE(raycast(s.world, {0, 0}, {1, 0}, NAN, 1));
  EXPECT_TRUE(overlapping(s.world, Shape::box({NAN, 0}, {1, 1}), 1).empty());
}

TEST(Queries, BoxesComeBeforeCirclesAndEachEntityOnce) {
  Scene s;
  const EntityId round = s.circle(0, 0, 5);
  const EntityId square = s.box(0, 0, 5);
  const EntityId both = s.box(1, 0, 5);
  s.world.addComponent<CircleColliderComponent>(both).radius = 5;
  EXPECT_EQ(overlapping(s.world, Shape::circle({0, 0}, 1), 1), (std::vector<EntityId>{square, both, round}));
  EXPECT_EQ(overlapping(s.world, Shape::circle({0, 0}, 1), 1, square), (std::vector<EntityId>{both, round}));
  EXPECT_TRUE(overlapping(s.world, Shape::circle({0, 0}, 1), 0).empty());
}

TEST(Queries, NearbyFindsTheNearMissesNearestFirst) {
  Scene s;
  s.world.registerComponent<GroundComponent>();
  const EntityId hero = s.box(0, 0, 5);
  s.world.addTag(hero, "Hero");
  const EntityId coin = s.box(10.01f, 0, 5);  // a hair away
  s.box(50, 0, 5);                            // far
  const EntityId stuck = s.circle(0, 6, 2);    // in it by 1
  const EntityId ground = s.world.createEntity();
  s.world.addComponent<TransformComponent>(ground).position = {0, -5.5f, 0};
  s.world.addComponent<GroundComponent>(ground).chains.emplace_back(std::vector<glm::vec2>{{-20, 0}, {20, 0}}, false, false);
  const auto near = nearby(s.world, "Hero", 1);
  ASSERT_EQ(near.size(), 3u);
  EXPECT_EQ(near[0].entity, stuck);
  EXPECT_NEAR(near[0].gap, -1, 1e-4f);
  EXPECT_EQ(near[1].entity, coin);
  EXPECT_NEAR(near[1].gap, 0.01f, 1e-4f);
  EXPECT_EQ(near[2].entity, ground);
  EXPECT_STREQ(near[2].kind, "terrain");
  EXPECT_NEAR(near[2].gap, 0.5f, 1e-4f);
}
