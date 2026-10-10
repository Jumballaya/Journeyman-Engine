#include <gtest/gtest.h>

#include "Blocking.hpp"
#include "BoxColliderComponent.hpp"
#include "Terrain.hpp"
#include "TransformComponent.hpp"

// moveBlocked near terrain: walking on drawn ground.
namespace {

struct Level {
  World world;
  Level() {
    world.registerComponent<TransformComponent>();
    world.registerComponent<BoxColliderComponent>();
    world.registerComponent<TerrainComponent>();
  }
  EntityId ground(std::vector<glm::vec2> points, bool oneWay = false, uint32_t layer = kTerrainLayer) {
    const EntityId id = world.createEntity();
    world.addComponent<TransformComponent>(id);
    auto& t = world.addComponent<TerrainComponent>(id);
    t.chains.emplace_back(std::move(points), false, oneWay);
    t.layerMask = layer;
    return id;
  }
  // A 10x20 body standing (feet) at (x, y).
  EntityId body(float x, float y, uint32_t layer = 1) {
    const EntityId id = world.createEntity();
    world.addComponent<TransformComponent>(id).position = {x, y + 10.0f, 0.0f};
    auto& c = world.addComponent<BoxColliderComponent>(id);
    c.halfExtents = {5, 10};
    c.layerMask = layer;
    return id;
  }
  EntityId wall(glm::vec2 center, glm::vec2 half) {
    const EntityId id = world.createEntity();
    world.addComponent<TransformComponent>(id).position = {center, 0.0f};
    auto& c = world.addComponent<BoxColliderComponent>(id);
    c.halfExtents = half;
    c.layerMask = 1u << 31;
    c.blocksMask = 0xFFFFFFFFu;
    return id;
  }
  glm::vec2 feet(EntityId id) { return glm::vec2(world.getComponent<TransformComponent>(id)->position) - glm::vec2(0, 10); }
};

}  // namespace

TEST(Walking, FallsOntoGroundAndStandsOnIt) {
  Level l;
  const EntityId floor = l.ground({{-100, 0}, {100, 0}});
  const EntityId p = l.body(0, 30);
  const BlockedMove m = moveBlocked(l.world, p, {0, -50});
  EXPECT_EQ(m.hit.y, -1);
  EXPECT_EQ(m.hitY, floor);
  EXPECT_EQ(m.normal, glm::vec2(0, 1));
  EXPECT_NEAR(l.feet(p).y, 0, 0.02f);
}

TEST(Walking, WalksUpAndDownASlopeStayingOnIt) {
  Level l;
  l.ground({{-100, 0}, {0, 0}, {100, 50}, {200, 0}, {300, 0}});  // a hill, 26.6° each side
  const EntityId p = l.body(-20, 0);
  moveBlocked(l.world, p, {0, -1});
  for (int frame = 0; frame < 160; ++frame) {
    const BlockedMove m = moveBlocked(l.world, p, {2, -1});  // walking right, with a little gravity
    ASSERT_EQ(m.hit.y, -1) << "left the ground at x " << l.feet(p).x;
    EXPECT_EQ(m.hit.x, 0);
  }
  EXPECT_NEAR(l.feet(p).x, 300, 0.1f);
  EXPECT_NEAR(l.feet(p).y, 0, 0.05f);
  // On the way up, the ground under it leans: its normal points up and back.
  Level up;
  up.ground({{-100, 0}, {0, 0}, {100, 50}});
  const EntityId q = up.body(40, 22.52f);  // on its uphill corner (x 45)
  const BlockedMove m = moveBlocked(up.world, q, {1, -1});
  ASSERT_EQ(m.hit.y, -1);
  EXPECT_LT(m.normal.x, -0.4f);
  EXPECT_GT(m.normal.y, 0.8f);
}

TEST(Walking, GoingDownhillWithoutGravityStillKeepsItsFeetOnTheGround) {
  Level l;
  l.ground({{-100, 0}, {0, 0}, {100, -50}});
  const EntityId p = l.body(-10, 0);
  for (int frame = 0; frame < 40; ++frame) ASSERT_EQ(moveBlocked(l.world, p, {2, 0}).hit.y, -1);
  // Off a ledge, though, it falls.
  Level ledge;
  ledge.ground({{-100, 0}, {0, 0}});
  const EntityId q = ledge.body(-10, 0);
  moveBlocked(ledge.world, q, {30, 0});
  EXPECT_EQ(moveBlocked(ledge.world, q, {0, 0}).hit.y, 0);
  EXPECT_NEAR(ledge.feet(q).y, 0, 0.05f);
}

TEST(Walking, GroundSteeperThanFiftyDegreesIsAWall) {
  Level l;
  const EntityId cliff = l.ground({{-100, 0}, {0, 0}, {20, 60}});  // 71.6°
  const EntityId p = l.body(-20, 0);
  moveBlocked(l.world, p, {0, -1});
  const BlockedMove m = moveBlocked(l.world, p, {40, -1});
  EXPECT_EQ(m.hit.x, 1);
  EXPECT_EQ(m.hitX, cliff);
  EXPECT_LT(l.feet(p).x, 0);  // its right side (x + 5) against the cliff's foot
  EXPECT_GT(l.feet(p).x, -5.1f);
  EXPECT_NEAR(l.feet(p).y, 0, 0.05f);
  EXPECT_EQ(moveBlocked(l.world, p, {1, -1}).normal, glm::vec2(0, 1));  // at its foot, on the flat
}

TEST(Walking, OneWayPlatformsHoldFromAboveOnly) {
  Level l;
  const EntityId shelf = l.ground({{-50, 40}, {50, 40}}, true);
  l.ground({{-200, 0}, {200, 0}});
  const EntityId p = l.body(0, 0);
  EXPECT_EQ(moveBlocked(l.world, p, {0, 60}).hit.y, 0);  // jumps up through it
  const BlockedMove land = moveBlocked(l.world, p, {0, -30});
  EXPECT_EQ(land.hitY, shelf);
  EXPECT_NEAR(l.feet(p).y, 40, 0.05f);
  EXPECT_EQ(moveBlocked(l.world, p, {0, -30}, 0, true).hit.y, 0);  // drops through
  EXPECT_NEAR(l.feet(p).y, 10, 0.05f);
  // Walking under one at head height, it isn't in the way.
  const EntityId q = l.body(-100, 0);
  EXPECT_EQ(moveBlocked(l.world, q, {200, -1}).hit.x, 0);
  EXPECT_NEAR(l.feet(q).x, 100, 0.05f);
}

TEST(Walking, ACeilingStopsAJump) {
  Level l;
  const EntityId roof = l.ground({{-50, 50}, {50, 50}});
  const EntityId p = l.body(0, 0);
  const BlockedMove m = moveBlocked(l.world, p, {0, 100});
  EXPECT_EQ(m.hit.y, 1);
  EXPECT_EQ(m.hitY, roof);
  EXPECT_EQ(m.normal, glm::vec2(0, -1));
  EXPECT_NEAR(l.feet(p).y, 30, 0.05f);
}

TEST(Walking, SolidBoxesNearTerrainAreStillWallsNotSteps) {
  Level l;
  l.ground({{-200, 0}, {200, 0}});
  const EntityId step = l.wall({20, 2}, {5, 2});  // only 4 tall, but a box
  const EntityId p = l.body(0, 0);
  moveBlocked(l.world, p, {0, -1});
  const BlockedMove m = moveBlocked(l.world, p, {20, -1});
  EXPECT_EQ(m.hitX, step);
  EXPECT_NEAR(l.feet(p).x, 10, 0.05f);
}

TEST(Walking, TerrainOnOtherLayersAndTerrainItStartsInDontStopIt) {
  Level l;
  l.ground({{-100, 0}, {100, 0}}, false, 1u << 3);
  const EntityId p = l.body(0, 10);
  EXPECT_EQ(moveBlocked(l.world, p, {0, -50}).hit.y, 0);
  Level in;
  in.ground({{-100, 5}, {100, 5}});  // through its middle
  const EntityId q = in.body(0, 0);
  EXPECT_EQ(moveBlocked(in.world, q, {0, 50}).hit.y, 0);
}

TEST(Walking, AFastMoveDoesntPassThroughAThinWall) {
  Level l;
  l.ground({{-500, 0}, {500, 0}});
  const EntityId wall = l.ground({{100, -10}, {100, 100}});
  const EntityId p = l.body(0, 0);
  moveBlocked(l.world, p, {0, -1});
  const BlockedMove m = moveBlocked(l.world, p, {400, -1});
  EXPECT_EQ(m.hitX, wall);
  EXPECT_NEAR(l.feet(p).x, 95, 0.05f);
}
