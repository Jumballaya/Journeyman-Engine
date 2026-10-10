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
  EntityId body(float x, float y, uint32_t layer = 1) { return mover({x, y + 10.0f}, {5, 10}, layer); }
  EntityId mover(glm::vec2 center, glm::vec2 half, uint32_t layer = 1) {
    const EntityId id = world.createEntity();
    world.addComponent<TransformComponent>(id).position = {center, 0.0f};
    auto& c = world.addComponent<BoxColliderComponent>(id);
    c.halfExtents = half;
    c.layerMask = layer;
    return id;
  }
  glm::vec2 at(EntityId id) { return glm::vec2(world.getComponent<TransformComponent>(id)->position); }
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

TEST(Walking, WalksOffTheTopOfASlopeEndingInACliffAtAnySpeed) {
  for (const float speed : {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f}) {
    Level l;
    l.ground({{-50, 0}, {0, 0}, {100, 40}, {110, -60}});
    const EntityId p = l.body(-20, 0);
    for (int frame = 0; frame < 400 && l.feet(p).x < 120; ++frame) moveBlocked(l.world, p, {speed, -2});
    EXPECT_GE(l.feet(p).x, 120) << "stuck at speed " << speed;
  }
}

TEST(Walking, ClimbingNeverPassesACeiling) {
  Level low;  // a short body walking under a ceiling that comes down to meet it
  low.ground({{-50, 0}, {400, 0}});
  const EntityId roof = low.ground({{20, 3}, {300, 1.5f}});
  const EntityId p = low.mover({0, 1.01f}, {4, 1});
  BlockedMove m;
  for (int frame = 0; frame < 100 && m.hit.x == 0; ++frame) m = moveBlocked(low.world, p, {3, -1});
  EXPECT_EQ(m.hitX, roof);
  EXPECT_LT(low.at(p).y + 1, 3);
  Level slope;  // walking up a slope into a flat ceiling
  slope.ground({{-100, -100}, {100, 100}});
  slope.ground({{-50, 7.5f}, {50, 7.5f}});
  const EntityId q = slope.mover({0, 6.01f}, {5, 1});
  moveBlocked(slope.world, q, {4, 0});
  EXPECT_LE(slope.at(q).y + 1, 7.5f);
}

TEST(Walking, WallsStopFastAndTinyMoversExactlyAtTheNearestOne) {
  Level l;
  l.ground({{-100, 0}, {5000, 0}});
  const EntityId nearer = l.ground({{6, -10}, {6, 30}});
  l.ground({{8, -10}, {8, 30}});
  const EntityId p = l.body(0, 0);
  const BlockedMove m = moveBlocked(l.world, p, {4096, -1});
  EXPECT_EQ(m.hitX, nearer);
  EXPECT_NEAR(l.feet(p).x, 1, 0.02f);
  Level tiny;
  const EntityId wall = tiny.ground({{0.5f, -10}, {0.5f, 10}});
  const EntityId q = tiny.mover({0, 0}, {0.1f, 1});
  EXPECT_EQ(moveBlocked(tiny.world, q, {1, 0}).hitX, wall);
  EXPECT_LT(tiny.at(q).x, 0.4f);
}

TEST(Walking, OneWayEdgesNeverHoldItBackOrUpWhenDroppingThrough) {
  Level l;
  l.ground({{-10, -10}, {10, 10}}, true);
  const EntityId p = l.mover({0, 2.01f}, {1, 1});
  moveBlocked(l.world, p, {1, -0.1f}, 0, true);
  EXPECT_NEAR(l.at(p).y, 1.91f, 0.01f);  // not lifted up the one-way slope
  Level steep;  // a steep one-way bit on a solid slope doesn't stop it climbing
  steep.ground({{-10, -10}, {10, 10}});
  steep.ground({{1.2f, 1.4f}, {1.5f, 2.1f}}, true);
  const EntityId q = steep.mover({0, 2.01f}, {1, 1});
  EXPECT_EQ(moveBlocked(steep.world, q, {1, -0.1f}).hit.x, 0);
  EXPECT_NEAR(steep.at(q).x, 1, 0.01f);
}

TEST(Walking, TheSnapFollowsOnlyHowFarItReallyWent) {
  Level l;
  l.ground({{-100, 0}, {0, 0}});
  l.ground({{-100, -50}, {100, -50}});
  l.ground({{11, -60}, {11, 100}});
  const EntityId p = l.mover({4, 10.01f}, {5, 10});
  moveBlocked(l.world, p, {100, 0});  // the wall lets it go 2: off the ledge, not down to the floor below
  EXPECT_GT(l.at(p).y, 0);
}

TEST(Walking, DegenerateGroundAndBodiesStillLand) {
  Level l;
  l.ground({{-10, -10}, {0, 0}, {0, 0}, {10, -10}});  // a repeated point
  const EntityId p = l.mover({0, 3}, {1, 1});
  const BlockedMove m = moveBlocked(l.world, p, {0, -5});
  ASSERT_EQ(m.hit.y, -1);
  EXPECT_FALSE(std::isnan(m.normal.x));
  Level point;
  point.ground({{-10, 0}, {10, 0}});
  const EntityId q = point.mover({0, 40}, {0, 0});
  EXPECT_EQ(moveBlocked(point.world, q, {0, -80}).hit.y, -1);
  EXPECT_NEAR(point.at(q).y, 0, 0.05f);
}
