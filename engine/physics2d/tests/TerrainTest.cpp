#include <gtest/gtest.h>

#include "BoxColliderComponent.hpp"
#include "CircleColliderComponent.hpp"
#include "Queries.hpp"
#include "Terrain.hpp"
#include "TransformComponent.hpp"

TEST(Terrain, RaysHitASegmentFromEitherSideFacingTheRay) {
  const auto down = raycastSegment({-10, 0}, {10, 0}, false, {0, 5}, {0, -1}, 100);
  ASSERT_TRUE(down);
  EXPECT_FLOAT_EQ(down->distance, 5);
  EXPECT_EQ(down->normal, glm::vec2(0, 1));
  const auto up = raycastSegment({10, 0}, {-10, 0}, false, {0, -5}, {0, 1}, 100);
  ASSERT_TRUE(up);
  EXPECT_EQ(up->normal, glm::vec2(0, -1));
  // A slope's normal leans: a 45° hill rising to the right faces up-left.
  const auto slope = raycastSegment({0, 0}, {10, 10}, false, {5, 20}, {0, -1}, 100);
  ASSERT_TRUE(slope);
  EXPECT_FLOAT_EQ(slope->distance, 15);
  EXPECT_NEAR(slope->normal.x, -0.7071f, 1e-4f);
  EXPECT_NEAR(slope->normal.y, 0.7071f, 1e-4f);
  EXPECT_FALSE(raycastSegment({-10, 0}, {10, 0}, false, {0, 5}, {1, 0}, 100));   // parallel
  EXPECT_FALSE(raycastSegment({-10, 0}, {10, 0}, false, {20, 5}, {0, -1}, 100));  // beside its end
  EXPECT_FALSE(raycastSegment({-10, 0}, {10, 0}, false, {0, 5}, {0, -1}, 4.9f));  // out of reach
}

TEST(Terrain, AOneWaySegmentHoldsOnlyRaysHeadingDownOntoItsTop) {
  EXPECT_TRUE(raycastSegment({10, 0}, {-10, 0}, true, {0, 5}, {0, -1}, 100));    // drawn either way round
  EXPECT_FALSE(raycastSegment({-10, 0}, {10, 0}, true, {0, -5}, {0, 1}, 100));   // from below: through it
  const auto above = raycastSegment({-10, 0}, {10, 0}, true, {0, 5}, glm::normalize(glm::vec2(1, -1)), 100);
  ASSERT_TRUE(above);
  EXPECT_EQ(above->normal, glm::vec2(0, 1));
  // A slope: sideways or rising rays pass, whichever way it's drawn.
  for (const bool reversed : {false, true}) {
    const glm::vec2 a = reversed ? glm::vec2(10, 10) : glm::vec2(0, 0), b = reversed ? glm::vec2(0, 0) : glm::vec2(10, 10);
    EXPECT_FALSE(raycastSegment(a, b, true, {0, 5}, {1, 0}, 100));
    EXPECT_FALSE(raycastSegment(a, b, true, {0, 1}, glm::normalize(glm::vec2(2, 1)), 100));
    EXPECT_TRUE(raycastSegment(a, b, true, {5, 20}, {0, -1}, 100));
  }
  // Heading down, but through it from underneath: passes.
  EXPECT_FALSE(raycastSegment({-20, -20}, {20, 20}, true, {5, 0}, glm::normalize(glm::vec2(-1, -0.1f)), 100));
  EXPECT_FALSE(raycastSegment({0, 0}, {10, 10}, true, {8, 6}, glm::normalize(glm::vec2(-2, -1)), 10));
  // An upright one has no top: nothing stops on it.
  EXPECT_FALSE(raycastSegment({0, -10}, {0, 10}, true, {-5, 0}, {1, 0}, 100));
  EXPECT_FALSE(raycastSegment({0, 10}, {0, -10}, true, {5, 0}, {-1, 0}, 100));
}

TEST(Terrain, ARayStartingOnASegmentDoesntCrossIt) {
  EXPECT_FALSE(raycastSegment({-10, 0}, {10, 0}, false, {0, 0}, {0, -1}, 100));
  EXPECT_FALSE(raycastSegment({-10, 0}, {10, 0}, false, {0, 0}, {0, 1}, 100));
  EXPECT_TRUE(raycastSegment({-10, 0}, {10, 0}, false, {0, 0.01f}, {0, -1}, 100));
}

TEST(Terrain, BigCoordinatesAndBrokenSegmentsDontCorruptHits) {
  const auto wide = raycastSegment({-1e20f, 0}, {1e20f, 0}, true, {0, 10}, {0, -1}, 10);
  ASSERT_TRUE(wide);
  EXPECT_EQ(wide->normal, glm::vec2(0, 1));
  EXPECT_FALSE(raycastSegment({-INFINITY, 0}, {10, 0}, false, {0, 5}, {0, -1}, 100));
  EXPECT_FALSE(raycastSegment({NAN, 0}, {10, 0}, false, {0, 5}, {0, -1}, 100));
  // Far out on a wide map, a ray cast again from where one hit a slope still starts on it.
  for (float x = 8000; x < 32000; x += 997) {
    const glm::vec2 a(x, 0), b(x + 100, 37), down(0, -1);
    const auto hit = raycastSegment(a, b, false, {x + 50, 100}, down, 200);
    ASSERT_TRUE(hit);
    const glm::vec2 at = glm::vec2(x + 50, 100) + down * hit->distance;
    EXPECT_FALSE(raycastSegment(a, b, false, at, glm::normalize(glm::vec2(1, -1)), 200)) << x;
  }
}

TEST(Terrain, BoxesAndCirclesOverlapSegmentsThatCrossThem) {
  EXPECT_TRUE(overlapsSegment(Shape::box({0, 0}, {5, 5}), {-10, 2}, {10, 2}));
  EXPECT_FALSE(overlapsSegment(Shape::box({0, 0}, {5, 5}), {-10, 5}, {10, 5}));  // along its edge
  EXPECT_TRUE(overlapsSegment(Shape::circle({0, 0}, 3), {-10, 2}, {10, 2}));
  EXPECT_FALSE(overlapsSegment(Shape::circle({0, 0}, 3), {-10, 3}, {10, 3}));   // touching
}

namespace {

struct Level {
  World world;
  Level() {
    world.registerComponent<TransformComponent>();
    world.registerComponent<BoxColliderComponent>();
    world.registerComponent<CircleColliderComponent>();
    world.registerComponent<TerrainComponent>();
  }
  EntityId ground(glm::vec2 at, std::vector<glm::vec2> points, bool closed = false, bool oneWay = false) {
    const EntityId id = world.createEntity();
    world.addComponent<TransformComponent>(id).position = {at, 0};
    world.addComponent<TerrainComponent>(id).chains.push_back({std::move(points), closed, oneWay});
    return id;
  }
};

}  // namespace

TEST(Terrain, QueriesFindTheGroundBeneathAndWhatsInAnArea) {
  Level l;
  const EntityId hill = l.ground({100, 0}, {{-50, 0}, {0, 20}, {50, 0}});
  const EntityId rock = l.ground({300, 0}, {{0, 0}, {10, 0}, {10, 10}, {0, 10}}, true);
  auto hit = raycast(l.world, {100, 50}, {0, -1}, 100, 1);  // straight down onto the hill's top
  ASSERT_TRUE(hit);
  EXPECT_EQ(hit->entity, hill);
  EXPECT_FLOAT_EQ(hit->distance, 30);
  hit = raycast(l.world, {280, 5}, {1, 0}, 100, 1);  // the closed rock's left side
  ASSERT_TRUE(hit);
  EXPECT_EQ(hit->entity, rock);
  EXPECT_FLOAT_EQ(hit->point.x, 300);
  hit = raycast(l.world, {320, 5}, {-1, 0}, 100, 1);  // and its right side: closed, so the last point joins the first
  ASSERT_TRUE(hit);
  EXPECT_FLOAT_EQ(hit->point.x, 310);
  EXPECT_FALSE(raycast(l.world, {100, 50}, {0, -1}, 100, 2));  // not on that layer
  EXPECT_EQ(overlapping(l.world, Shape::circle({305, 5}, 8), 1), std::vector<EntityId>{rock});
  EXPECT_TRUE(overlapping(l.world, Shape::circle({305, 5}, 2), 1).empty());  // inside the rock, touching no line
}

TEST(Terrain, QueriesIgnoreTheGivenEntityAndRaysReachOnlyAsFarAsTheyGo) {
  Level l;
  const EntityId near = l.ground({0, 0}, {{-10, 0}, {10, 0}});
  const EntityId far = l.ground({0, -100}, {{-10, 0}, {10, 0}});
  auto hit = raycast(l.world, {0, 10}, {0, -1}, INFINITY, 1, near);
  ASSERT_TRUE(hit);
  EXPECT_EQ(hit->entity, far);
  EXPECT_FALSE(raycast(l.world, {0, 10}, {0, -1}, 50, 1, near));
  EXPECT_TRUE(overlapping(l.world, Shape::box({0, 0}, {5, 5}), 1, near).empty());
}
