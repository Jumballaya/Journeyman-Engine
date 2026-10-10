#include <gtest/gtest.h>

#include "Blocking.hpp"
#include "BoxColliderComponent.hpp"
#include "Terrain.hpp"
#include "TransformComponent.hpp"

// walkBlocked: walking on drawn ground.
namespace {

struct Level {
  World world;
  Level() {
    world.registerComponent<TransformComponent>();
    world.registerComponent<BoxColliderComponent>();
    world.registerComponent<GroundComponent>();
  }
  EntityId ground(std::vector<glm::vec2> points, bool oneWay = false, uint32_t layer = kTerrainLayers) {
    const EntityId id = world.createEntity();
    world.addComponent<TransformComponent>(id);
    auto& t = world.addComponent<GroundComponent>(id);
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
  const BlockedMove m = walkBlocked(l.world, p, {0, -50});
  EXPECT_EQ(m.hit.y, -1);
  EXPECT_EQ(m.hitY, floor);
  EXPECT_EQ(m.normal, glm::vec2(0, 1));
  EXPECT_NEAR(l.feet(p).y, 0, 0.02f);
}

TEST(Walking, WalksUpAndDownASlopeStayingOnIt) {
  Level l;
  l.ground({{-100, 0}, {0, 0}, {100, 50}, {200, 0}, {300, 0}});  // a hill, 26.6° each side
  const EntityId p = l.body(-20, 0);
  walkBlocked(l.world, p, {0, -1});
  for (int frame = 0; frame < 160; ++frame) {
    const BlockedMove m = walkBlocked(l.world, p, {2, -1});  // walking right, with a little gravity
    ASSERT_EQ(m.hit.y, -1) << "left the ground at x " << l.feet(p).x;
    EXPECT_EQ(m.hit.x, 0);
  }
  EXPECT_NEAR(l.feet(p).x, 300, 0.1f);
  EXPECT_NEAR(l.feet(p).y, 0, 0.05f);
  // On the way up, the ground under it leans: its normal points up and back.
  Level up;
  up.ground({{-100, 0}, {0, 0}, {100, 50}});
  const EntityId q = up.body(40, 22.52f);  // on its uphill corner (x 45)
  const BlockedMove m = walkBlocked(up.world, q, {1, -1});
  ASSERT_EQ(m.hit.y, -1);
  EXPECT_LT(m.normal.x, -0.4f);
  EXPECT_GT(m.normal.y, 0.8f);
}

TEST(Walking, GoingDownhillWithoutGravityStillKeepsItsFeetOnTheGround) {
  Level l;
  l.ground({{-100, 0}, {0, 0}, {100, -50}});
  const EntityId p = l.body(-10, 0);
  for (int frame = 0; frame < 40; ++frame) ASSERT_EQ(walkBlocked(l.world, p, {2, 0}).hit.y, -1);
  // Off a ledge, though, it falls.
  Level ledge;
  ledge.ground({{-100, 0}, {0, 0}});
  const EntityId q = ledge.body(-10, 0);
  walkBlocked(ledge.world, q, {30, 0});
  EXPECT_EQ(walkBlocked(ledge.world, q, {0, 0}).hit.y, 0);
  EXPECT_NEAR(ledge.feet(q).y, 0, 0.05f);
}

TEST(Walking, GroundSteeperThanFiftyDegreesIsAWall) {
  Level l;
  const EntityId cliff = l.ground({{-100, 0}, {0, 0}, {20, 60}});  // 71.6°
  const EntityId p = l.body(-20, 0);
  walkBlocked(l.world, p, {0, -1});
  const BlockedMove m = walkBlocked(l.world, p, {40, -1});
  EXPECT_EQ(m.hit.x, 1);
  EXPECT_EQ(m.hitX, cliff);
  EXPECT_LT(l.feet(p).x, 0);  // its right side (x + 5) against the cliff's foot
  EXPECT_GT(l.feet(p).x, -5.1f);
  EXPECT_NEAR(l.feet(p).y, 0, 0.05f);
  EXPECT_EQ(walkBlocked(l.world, p, {1, -1}).normal, glm::vec2(0, 1));  // at its foot, on the flat
}

TEST(Walking, OneWayPlatformsHoldFromAboveOnly) {
  Level l;
  const EntityId shelf = l.ground({{-50, 40}, {50, 40}}, true);
  l.ground({{-200, 0}, {200, 0}});
  const EntityId p = l.body(0, 0);
  EXPECT_EQ(walkBlocked(l.world, p, {0, 60}).hit.y, 0);  // jumps up through it
  const BlockedMove land = walkBlocked(l.world, p, {0, -30});
  EXPECT_EQ(land.hitY, shelf);
  EXPECT_NEAR(l.feet(p).y, 40, 0.05f);
  EXPECT_EQ(walkBlocked(l.world, p, {0, -30}, true).hit.y, 0);  // drops through
  EXPECT_NEAR(l.feet(p).y, 10, 0.05f);
  // Walking under one at head height, it isn't in the way.
  const EntityId q = l.body(-100, 0);
  EXPECT_EQ(walkBlocked(l.world, q, {200, -1}).hit.x, 0);
  EXPECT_NEAR(l.feet(q).x, 100, 0.05f);
}

TEST(Walking, ACeilingStopsAJump) {
  Level l;
  const EntityId roof = l.ground({{-50, 50}, {50, 50}});
  const EntityId p = l.body(0, 0);
  const BlockedMove m = walkBlocked(l.world, p, {0, 100});
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
  walkBlocked(l.world, p, {0, -1});
  const BlockedMove m = walkBlocked(l.world, p, {20, -1});
  EXPECT_EQ(m.hitX, step);
  EXPECT_NEAR(l.feet(p).x, 10, 0.05f);
}

TEST(Walking, TerrainOnOtherLayersDoesntStopIt) {
  Level l;
  l.ground({{-100, 0}, {100, 0}}, false, 1u << 3);
  const EntityId p = l.body(0, 10);
  EXPECT_EQ(walkBlocked(l.world, p, {0, -50}).hit.y, 0);
}

TEST(Walking, ABodySpawnedInTheGroundEndsUpStandingOnIt) {
  Level l;
  const EntityId floor = l.ground({{-100, 0}, {0, 0}, {100, 0}});
  const EntityId p = l.body(0, -2);  // 2 units in, across a corner of the line
  for (int frame = 0; frame < 30; ++frame) walkBlocked(l.world, p, {0, -5});  // gravity
  EXPECT_NEAR(l.feet(p).x, 0, 0.001f);
  EXPECT_NEAR(l.feet(p).y, 0, 0.02f);
  EXPECT_EQ(walkBlocked(l.world, p, {0, -5}).hitY, floor);
  Level moving;  // move() too
  moving.ground({{-100, 0}, {100, 0}});
  const EntityId q = moving.body(0, -2);
  moveBlocked(moving.world, q, {0, -5});
  EXPECT_NEAR(moving.feet(q).y, 0, 0.02f);
}

TEST(Walking, ABodyInTerrainLeavesItTheShortestWay) {
  Level l;
  l.ground({{0, -100}, {0, 100}});  // a wall it's 2 units into
  const EntityId p = l.mover({3, 0}, {5, 10});
  walkBlocked(l.world, p, {0, 0});
  EXPECT_NEAR(l.at(p).x, 5, 0.02f);
  EXPECT_NEAR(l.at(p).y, 0, 0.001f);
  Level slope;  // a gentle slope: straight up onto it, not sideways down it
  slope.ground({{-100, -50}, {100, 50}});
  const EntityId q = slope.mover({0, 0}, {2, 2});
  walkBlocked(slope.world, q, {0, -1});
  EXPECT_EQ(walkBlocked(slope.world, q, {0, -1}).hit.y, -1);
  EXPECT_NEAR(slope.at(q).x, 0, 0.001f);
  EXPECT_NEAR(slope.at(q).y, 3, 0.05f);  // its lower corner on the line
}

TEST(Walking, AOneWayPlatformItsInDoesntPushItOut) {
  Level l;
  l.ground({{-100, 5}, {100, 5}}, true);  // jumping up through it
  const EntityId p = l.body(0, 0);
  EXPECT_EQ(walkBlocked(l.world, p, {0, 2}).hit.y, 0);
  EXPECT_NEAR(l.feet(p).y, 2, 0.001f);
}

TEST(Walking, ABodyOnItsOwnLayerStillLandsOnGround) {
  Level l;
  l.ground({{-100, 0}, {100, 0}});  // on every layer, as terrain is unless narrowed
  const EntityId p = l.body(0, 10, 1u << 2);
  EXPECT_EQ(moveBlocked(l.world, p, {0, -50}).hit.y, -1);
  EXPECT_NEAR(l.feet(p).y, 0, 0.02f);
}

TEST(Walking, AFastMoveDoesntPassThroughAThinWall) {
  Level l;
  l.ground({{-500, 0}, {500, 0}});
  const EntityId wall = l.ground({{100, -10}, {100, 100}});
  const EntityId p = l.body(0, 0);
  walkBlocked(l.world, p, {0, -1});
  const BlockedMove m = walkBlocked(l.world, p, {400, -1});
  EXPECT_EQ(m.hitX, wall);
  EXPECT_NEAR(l.feet(p).x, 95, 0.05f);
}

TEST(Walking, WalksOffTheTopOfASlopeEndingInACliffAtAnySpeed) {
  for (const float speed : {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f}) {
    Level l;
    l.ground({{-50, 0}, {0, 0}, {100, 40}, {110, -60}});
    const EntityId p = l.body(-20, 0);
    for (int frame = 0; frame < 400 && l.feet(p).x < 120; ++frame) walkBlocked(l.world, p, {speed, -2});
    EXPECT_GE(l.feet(p).x, 120) << "stuck at speed " << speed;
  }
}

TEST(Walking, ClimbingNeverPassesACeiling) {
  Level low;  // a short body walking under a ceiling that comes down to meet it
  low.ground({{-50, 0}, {400, 0}});
  const EntityId roof = low.ground({{20, 3}, {300, 1.5f}});
  const EntityId p = low.mover({0, 1.01f}, {4, 1});
  BlockedMove m;
  for (int frame = 0; frame < 100 && m.hit.x == 0; ++frame) m = walkBlocked(low.world, p, {3, -1});
  EXPECT_EQ(m.hitX, roof);
  EXPECT_LT(low.at(p).y + 1, 3);
  Level slope;  // walking up a slope into a flat ceiling
  slope.ground({{-100, -100}, {100, 100}});
  slope.ground({{-50, 7.5f}, {50, 7.5f}});
  const EntityId q = slope.mover({0, 6.01f}, {5, 1});
  walkBlocked(slope.world, q, {4, 0});
  EXPECT_LE(slope.at(q).y + 1, 7.5f);
}

TEST(Walking, WallsStopFastAndTinyMoversExactlyAtTheNearestOne) {
  Level l;
  l.ground({{-100, 0}, {5000, 0}});
  const EntityId nearer = l.ground({{6, -10}, {6, 30}});
  l.ground({{8, -10}, {8, 30}});
  const EntityId p = l.body(0, 0);
  const BlockedMove m = walkBlocked(l.world, p, {4096, -1});
  EXPECT_EQ(m.hitX, nearer);
  EXPECT_NEAR(l.feet(p).x, 1, 0.02f);
  Level tiny;
  const EntityId wall = tiny.ground({{0.5f, -10}, {0.5f, 10}});
  const EntityId q = tiny.mover({0, 0}, {0.1f, 1});
  EXPECT_EQ(walkBlocked(tiny.world, q, {1, 0}).hitX, wall);
  EXPECT_LT(tiny.at(q).x, 0.4f);
}

TEST(Walking, OneWayEdgesNeverHoldItBackOrUpWhenDroppingThrough) {
  Level l;
  l.ground({{-10, -10}, {10, 10}}, true);
  const EntityId p = l.mover({0, 2.01f}, {1, 1});
  walkBlocked(l.world, p, {1, -0.1f}, true);
  EXPECT_NEAR(l.at(p).y, 1.91f, 0.01f);  // not lifted up the one-way slope
  Level steep;  // a steep one-way bit on a solid slope doesn't stop it climbing
  steep.ground({{-10, -10}, {10, 10}});
  steep.ground({{1.2f, 1.4f}, {1.5f, 2.1f}}, true);
  const EntityId q = steep.mover({0, 2.01f}, {1, 1});
  EXPECT_EQ(walkBlocked(steep.world, q, {1, -0.1f}).hit.x, 0);
  EXPECT_NEAR(steep.at(q).x, 1, 0.01f);
}

TEST(Walking, TheSnapFollowsOnlyHowFarItReallyWent) {
  Level l;
  l.ground({{-100, 0}, {0, 0}});
  l.ground({{-100, -50}, {100, -50}});
  l.ground({{11, -60}, {11, 100}});
  const EntityId p = l.mover({4, 10.01f}, {5, 10});
  walkBlocked(l.world, p, {100, 0});  // the wall lets it go 2: off the ledge, not down to the floor below
  EXPECT_GT(l.at(p).y, 0);
}

TEST(Walking, DegenerateGroundAndBodiesStillLand) {
  Level l;
  l.ground({{-10, -10}, {0, 0}, {0, 0}, {10, -10}});  // a repeated point
  const EntityId p = l.mover({0, 3}, {1, 1});
  const BlockedMove m = walkBlocked(l.world, p, {0, -5});
  ASSERT_EQ(m.hit.y, -1);
  EXPECT_FALSE(std::isnan(m.normal.x));
  Level point;
  point.ground({{-10, 0}, {10, 0}});
  const EntityId q = point.mover({0, 40}, {0, 0});
  EXPECT_EQ(walkBlocked(point.world, q, {0, -80}).hit.y, -1);
  EXPECT_NEAR(point.at(q).y, 0, 0.05f);
}

TEST(Walking, TinyBodiesClimbAndLowCeilingsLetItGoAsFarAsItFits) {
  Level l;
  l.ground({{-10, -10}, {10, 10}});
  const EntityId p = l.mover({0, 0.21f}, {0.1f, 0.1f});
  walkBlocked(l.world, p, {1, -0.05f});
  EXPECT_GT(l.at(p).y, 1);  // up the slope, not under it
  Level low;
  low.ground({{-100, -100}, {100, 100}});
  low.ground({{-50, 10.9f}, {50, 10.9f}});
  const EntityId q = low.mover({0, 6.01f}, {5, 1});
  for (int frame = 0; frame < 5; ++frame) walkBlocked(low.world, q, {4, -1});
  EXPECT_GT(low.at(q).x, 3.5f);
  EXPECT_LE(low.at(q).y + 1, 10.9f);
  Level wide;  // a wide, short body doesn't climb onto a ceiling through its end
  wide.ground({{-100, 0}, {100, 0}});
  wide.ground({{-10, 10}, {5, 1.9f}});
  const EntityId r = wide.mover({10, 1.01f}, {5, 1});
  for (int frame = 0; frame < 5; ++frame) walkBlocked(wide.world, r, {-3, -0.1f});
  EXPECT_NEAR(wide.at(r).y, 1.01f, 0.01f);
}

TEST(Walking, ASteepOneWayBitDoesntSpoilClimbingTheGroundBeneathIt) {
  Level l;
  l.ground({{-100, -0.5f}, {100, 0.5f}});
  l.ground({{5.1f, 0.02f}, {5.11f, 0.056f}}, true);
  const EntityId p = l.mover({0, 1.035f}, {5, 1});
  EXPECT_EQ(walkBlocked(l.world, p, {4, -0.1f}).hit.x, 0);
  EXPECT_NEAR(l.at(p).x, 4, 0.01f);
}

TEST(Walking, MoveGoesExactlyAndNeverClimbs) {
  Level l;
  l.ground({{-100, 0}, {0, 0}, {100, 100}});  // flat, then a 45° slope
  const EntityId p = l.body(-20, 0.01f);
  const BlockedMove m = moveBlocked(l.world, p, {40, 0});
  EXPECT_EQ(m.hit.x, 1);  // the slope is a wall to it
  EXPECT_NEAR(l.feet(p).y, 0.01f, 1e-4f);
  EXPECT_LT(l.feet(p).x + 5, 0.02f);
}

TEST(Walking, TerrainElsewhereLeavesMovesAmongBoxesSliding) {
  Level l;
  l.ground({{1000, 0}, {1100, 0}});  // far away
  l.wall({-6, 30}, {5, 5});
  const EntityId p = l.mover({0, 0}, {2, 2});
  moveBlocked(l.world, p, {0, 40}, 3);  // blocked by the wall's corner: nudged right past it
  EXPECT_GT(l.at(p).y, 0);
  EXPECT_GT(l.at(p).x, 0);
}

TEST(Walking, WalkingOffACliffInOneLongStepDoesntSnapToTheBottom) {
  Level l;
  l.ground({{-100, 0}, {0, 0}});
  l.ground({{0, -30}, {100, -30}});
  const EntityId p = l.body(-10, 0.01f);
  const BlockedMove m = walkBlocked(l.world, p, {40, 0});
  EXPECT_EQ(m.hit.y, 0);  // in the air now, falling from here
  EXPECT_NEAR(l.feet(p).y, 0.01f, 0.02f);
  EXPECT_NEAR(l.feet(p).x, 30, 1e-3f);
}

TEST(Walking, DownhillLandsTheSameInOneStepOrMany) {
  Level one, many;
  for (Level* l : {&one, &many}) l->ground({{-100, 0}, {0, 0}, {100, -60}});
  const EntityId a = one.body(-10, 0.01f), b = many.body(-10, 0.01f);
  walkBlocked(one.world, a, {60, 0});
  for (int i = 0; i < 30; ++i) walkBlocked(many.world, b, {2, 0});
  EXPECT_NEAR(one.feet(a).x, many.feet(b).x, 1e-3f);
  EXPECT_NEAR(one.feet(a).y, many.feet(b).y, 0.05f);
  EXPECT_EQ(walkBlocked(one.world, a, {0, -0.5f}).hit.y, -1);  // on the slope, not over it
}

TEST(Walking, StepsDownAsHighAsItStepsUpAtAnySpeed) {
  for (const float dx : {0.5f, 2.0f, 4.0f}) {
    Level l;
    l.ground({{-100, 0}, {0, 0}, {0, -1}, {100, -1}});  // a 1-unit step down
    const EntityId p = l.body(-6, 0.01f);
    BlockedMove m;
    for (int i = 0; i < 40 / dx; ++i) m = walkBlocked(l.world, p, {dx, 0});
    EXPECT_EQ(m.hit.y, -1) << dx;
    EXPECT_NEAR(l.feet(p).y, -0.99f, 0.02f) << dx;
  }
}

TEST(Walking, LeavingTheGroundNeverPutsItInASolid) {
  Level l;
  l.ground({{-100, 0}, {100, 0}});
  const EntityId roof = l.wall({0, 20}, {100, 1});  // just over its head: no room to go up
  const EntityId p = l.mover({0, 8}, {5, 10});      // 2 units into the ground
  const BlockedMove m = moveBlocked(l.world, p, {0, 40});
  EXPECT_EQ(m.hitY, roof);
  EXPECT_LT(l.at(p).y + 10, 19);
}

TEST(Walking, ALiftInTheGroundGoesWhereItsSentWithItsRider) {
  Level l;
  l.ground({{-100, 0}, {100, 0}});
  const EntityId lift = l.wall({0, 0}, {10, 2});  // a solid mover, half in the ground
  const EntityId rider = l.body(0, 2.01f);
  moveBlocked(l.world, lift, {3, 0});
  EXPECT_EQ(l.at(lift), glm::vec2(3, 0));
  EXPECT_NEAR(l.at(rider).x, 3, 0.001f);
  EXPECT_NEAR(l.feet(rider).y, 2.01f, 0.001f);
}

TEST(Walking, MovingGroundInTheGroundGoesWhereItsSentWithItsRider) {
  Level l;
  l.ground({{-100, 0}, {100, 0}});
  const EntityId cart = l.mover({0, 0}, {10, 2});  // not solid: carries by its terrain
  l.world.addComponent<GroundComponent>(cart).chains.emplace_back(std::vector<glm::vec2>{{-10, 2}, {10, 2}}, false, false);
  const EntityId rider = l.body(0, 2.01f);
  moveBlocked(l.world, cart, {3, 0});
  EXPECT_EQ(l.at(cart), glm::vec2(3, 0));
  EXPECT_NEAR(l.at(rider).x, 3, 0.001f);
}
