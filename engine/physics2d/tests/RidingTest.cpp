#include <gtest/gtest.h>

#include "Blocking.hpp"
#include "BoxColliderComponent.hpp"
#include "Terrain.hpp"
#include "TransformComponent.hpp"
#include "VelocityComponent.hpp"

// moveBlocked carrying what stands on a moving platform.
namespace {

struct Yard {
  World world;
  Yard() {
    world.registerComponent<TransformComponent>();
    world.registerComponent<BoxColliderComponent>();
    world.registerComponent<TerrainComponent>();
    world.registerComponent<VelocityComponent>();
  }
  EntityId box(glm::vec2 center, glm::vec2 half, uint32_t blocks = 0) {
    const EntityId id = world.createEntity();
    world.addComponent<TransformComponent>(id).position = {center, 0.0f};
    auto& c = world.addComponent<BoxColliderComponent>(id);
    c.halfExtents = half;
    c.blocksMask = blocks;
    return id;
  }
  // A solid body that moves (so it rides), unlike a static wall.
  EntityId crate(glm::vec2 center, glm::vec2 half) {
    const EntityId id = box(center, half, 0xFFFFFFFFu);
    world.addComponent<VelocityComponent>(id);
    return id;
  }
  // A 20-wide lift whose top is at y, with a 10x20 body standing on it at x.
  EntityId lift(float y) { return box({0, y - 2}, {10, 2}, 0xFFFFFFFFu); }
  EntityId rider(float x, float y) { return box({x, y + 10.01f}, {5, 10}); }
  glm::vec2 at(EntityId id) { return glm::vec2(world.getComponent<TransformComponent>(id)->position); }
};

}  // namespace

TEST(Riding, ASolidPlatformCarriesWhatStandsOnIt) {
  Yard y;
  const EntityId lift = y.lift(0);
  const EntityId rider = y.rider(0, 0);
  const EntityId bystander = y.rider(40, 0);  // beside it, not on it
  moveBlocked(y.world, lift, {3, 7});
  EXPECT_NEAR(y.at(rider).x, 3, 1e-4f);
  EXPECT_NEAR(y.at(rider).y, 17.01f, 1e-3f);
  EXPECT_EQ(y.at(bystander), glm::vec2(40, 10.01f));
  moveBlocked(y.world, lift, {0, -20});  // and down
  EXPECT_NEAR(y.at(rider).y - 10, y.at(lift).y + 2, 0.02f);
}

TEST(Riding, ARiderStillMeetsWallsAndCarriesItsOwnRiders) {
  Yard y;
  const EntityId lift = y.lift(0);
  const EntityId crate = y.crate({0, 5.01f}, {5, 5});
  const EntityId top = y.rider(0, 10.02f);
  y.box({30, 30}, {10, 30}, 0xFFFFFFFFu);  // a wall the crate runs into
  moveBlocked(y.world, lift, {20, 0});
  EXPECT_EQ(y.at(lift).x, 20);
  EXPECT_NEAR(y.at(crate).x, 14.99f, 0.02f);  // stopped by the wall, left behind
  EXPECT_NEAR(y.at(top).x, y.at(crate).x, 1e-3f);
}

TEST(Riding, MovingTerrainCarriesToo) {
  Yard y;
  const EntityId ground = y.world.createEntity();
  y.world.addComponent<TransformComponent>(ground);
  y.world.addComponent<TerrainComponent>(ground).chains.emplace_back(std::vector<glm::vec2>{{-50, 0}, {50, 0}}, false, false);
  const EntityId rider = y.rider(0, 0);
  moveBlocked(y.world, ground, {0, 5});
  EXPECT_NEAR(y.at(rider).y, 15.01f, 1e-3f);
}

TEST(Riding, ANonSolidMoverCarriesNothing) {
  Yard y;
  const EntityId ghost = y.box({0, -2}, {10, 2});
  const EntityId rider = y.rider(0, 0);
  moveBlocked(y.world, ghost, {0, 5});
  EXPECT_EQ(y.at(rider), glm::vec2(0, 10.01f));
}

TEST(Riding, LiftsRaiseSolidRidersAndStopWithThemAtACeiling) {
  Yard y;
  const EntityId lift = y.lift(0);
  const EntityId crate = y.crate({0, 5.01f}, {5, 5});
  moveBlocked(y.world, lift, {0, 20});
  EXPECT_NEAR(y.at(lift).y, 18, 1e-3f);
  EXPECT_NEAR(y.at(crate).y, 25.01f, 1e-3f);
  y.box({0, 60}, {50, 5}, 0xFFFFFFFFu);  // a ceiling at 55: the crate's top meets it
  moveBlocked(y.world, lift, {0, 40});
  EXPECT_NEAR(y.at(crate).y + 5, 55, 0.02f);
  EXPECT_NEAR(y.at(lift).y + 2, y.at(crate).y - 5, 0.02f);  // still under it, not through it
}

TEST(Riding, EachRiderMovesOnceEvenOnTwoCratesOrInACycle) {
  Yard y;
  const EntityId lift = y.box({0, -2}, {30, 2}, 0xFFFFFFFFu);
  y.crate({-10, 5.01f}, {5, 5});
  y.crate({10, 5.01f}, {5, 5});
  const EntityId plank = y.box({0, 11.02f}, {15, 1});
  moveBlocked(y.world, lift, {1, 0});
  EXPECT_NEAR(y.at(plank).x, 1, 1e-4f);
  Yard flat;  // two zero-height solids "standing" on each other
  const EntityId a = flat.crate({0, 0}, {5, 0});
  flat.crate({3, 0}, {5, 0});
  moveBlocked(flat.world, a, {0.02f, 0});
  SUCCEED();
}

TEST(Riding, RidersRiseTogetherAndAllFollowWhereThePlatformReallyWent) {
  Yard y;
  const EntityId lift = y.box({0, -2}, {30, 2}, 0xFFFFFFFFu);
  const EntityId free = y.rider(-15, 0);
  const EntityId tight = y.rider(15, 0);
  y.box({15, 32.01f}, {5, 10}, 0xFFFFFFFFu);  // over `tight`: room to rise 2
  const BlockedMove m = moveBlocked(y.world, lift, {0, 10});
  EXPECT_NEAR(y.at(lift).y, 0, 0.02f);
  EXPECT_NEAR(y.at(free).y, y.at(tight).y, 1e-3f);  // neither left behind
  EXPECT_EQ(m.hit.y, 1);  // the lift was stopped (by what stopped its rider)

  Yard over;  // an overhang over the lift's end, clear of its rider
  const EntityId lift2 = over.box({0, -2}, {30, 2}, 0xFFFFFFFFu);
  const EntityId rider = over.rider(-15, 0);
  over.box({28, 10}, {5, 5}, 0xFFFFFFFFu);
  moveBlocked(over.world, lift2, {0, 20});
  EXPECT_NEAR(over.at(rider).y - 10, over.at(lift2).y + 2, 0.02f);  // still on it, not floating
}

TEST(Riding, AStackGoesAllTheWayDiagonally) {
  Yard y;
  const EntityId lift = y.lift(0);
  const EntityId crate = y.crate({0, 5.01f}, {5, 5});
  const EntityId top = y.rider(0, 10.02f);
  moveBlocked(y.world, lift, {3, 5});
  EXPECT_NEAR(y.at(crate).x, 3, 1e-4f);
  EXPECT_NEAR(y.at(top).x, 3, 1e-4f);
  EXPECT_NEAR(y.at(top).y, 25.03f, 1e-3f);
}

TEST(Riding, ACeilingOverWhereARiderIsGoingStopsADiagonalLift) {
  Yard y;
  const EntityId lift = y.lift(0);
  const EntityId crate = y.crate({0, 5.01f}, {5, 5});
  y.box({10, 17}, {5, 5}, 0xFFFFFFFFu);  // over x=10, not over x=0
  moveBlocked(y.world, lift, {10, 5});
  EXPECT_NEAR(y.at(crate).x, 10, 1e-4f);
  EXPECT_NEAR(y.at(crate).y + 5, 12, 0.02f);
  EXPECT_NEAR(y.at(crate).y - 5, y.at(lift).y + 2, 0.02f);  // still on it, not in it
}

TEST(Riding, APlankOnTwoCratesRisesWithThem) {
  Yard y;
  const EntityId lift = y.box({0, -2}, {30, 2}, 0xFFFFFFFFu);
  const EntityId left = y.crate({-10, 5.01f}, {5, 5});
  const EntityId right = y.crate({10, 5.01f}, {5, 5});
  const EntityId plank = y.crate({0, 11.02f}, {15, 1});
  moveBlocked(y.world, lift, {0, 5});
  for (const EntityId e : {left, right}) EXPECT_NEAR(y.at(e).y, 10.01f, 1e-3f);
  EXPECT_NEAR(y.at(plank).y, 16.02f, 1e-3f);
}

TEST(Riding, CratesSideBySideAllGoWhicheverIsListedFirst) {
  for (const bool leftFirst : {true, false}) {
    Yard y;
    const EntityId lift = y.box({0, -2}, {30, 2}, 0xFFFFFFFFu);
    EntityId left = kNoEntityId, right = kNoEntityId;
    if (leftFirst) left = y.crate({-10, 5.01f}, {5, 5});
    right = y.crate({0.02f, 5.01f}, {5, 5});  // nearly touching
    if (!leftFirst) left = y.crate({-10, 5.01f}, {5, 5});
    moveBlocked(y.world, lift, {20, 0});
    EXPECT_NEAR(y.at(left).x, 10, 1e-4f);
    EXPECT_NEAR(y.at(right).x, 20.02f, 1e-4f);
  }
}

TEST(Riding, RisingTerrainIsNoCeilingToItsOwnRiders) {
  Yard y;
  const EntityId ground = y.world.createEntity();
  y.world.addComponent<TransformComponent>(ground);
  auto& chains = y.world.addComponent<TerrainComponent>(ground).chains;
  chains.emplace_back(std::vector<glm::vec2>{{-50, 0}, {50, 0}}, false, false);
  chains.emplace_back(std::vector<glm::vec2>{{-50, 30}, {50, 30}}, false, false);
  const EntityId rider = y.rider(0, 0);
  const BlockedMove m = moveBlocked(y.world, ground, {0, 20});
  EXPECT_NEAR(y.at(rider).y, 30.01f, 1e-3f);
  EXPECT_EQ(m.hit.y, 0);
}

TEST(Riding, ARiderGoingDownStopsOnACrateThatStoppedOnALedge) {
  Yard y;
  const EntityId lift = y.lift(0);
  y.crate({-5, 5.01f}, {5, 5});
  const EntityId over = y.crate({10, 5.01f}, {5, 5});  // past the lift's end, over the ledge
  y.box({15.5f, -5}, {4.5f, 2}, 0xFFFFFFFFu);
  const EntityId plank = y.crate({2, 11.02f}, {10, 1});  // on both crates
  for (int i = 0; i < 5; ++i) moveBlocked(y.world, lift, {0, -3});
  EXPECT_NEAR(y.at(over).y - 5, -2.99f, 0.02f);
  EXPECT_GE(y.at(plank).y - 1, y.at(over).y + 5 - 1e-3f);  // on it, not in it
}

TEST(Riding, ACartCarriesARiderOffADockWithoutDroppingItThrough) {
  Yard y;
  const EntityId ground = y.world.createEntity();
  y.world.addComponent<TransformComponent>(ground);
  y.world.addComponent<TerrainComponent>(ground).chains.emplace_back(std::vector<glm::vec2>{{-100, -5}, {200, -5}}, false, false);
  y.box({-10, -3}, {10, 2}, 0xFFFFFFFFu);  // the dock
  const EntityId cart = y.box({10, -3}, {10, 2}, 0xFFFFFFFFu);
  const EntityId rider = y.rider(0, -1);  // half on each
  for (int i = 0; i < 8; ++i) moveBlocked(y.world, cart, {6, 0});
  EXPECT_NEAR(y.at(rider).x, 48, 1e-3f);
  EXPECT_NEAR(y.at(rider).y - 10, -0.99f, 1e-3f);
}

TEST(Riding, ACarrierRisesStraightAndReportsWhatStoppedItsRider) {
  Yard y;
  const EntityId lift = y.box({0, 0}, {1, 1}, 0xFFFFFFFFu);
  y.crate({0, 4.01f}, {0.2f, 3});
  y.box({0.75f, 5}, {0.25f, 1}, 0xFFFFFFFFu);  // a ceiling over the lift, beside its rider
  y.box({-2, 0}, {0.25f, 1}, 0xFFFFFFFFu);
  moveBlocked(y.world, lift, {0, 4}, 1.0f);  // no sliding aside under a rider
  EXPECT_EQ(y.at(lift).x, 0);

  Yard r;
  const EntityId lift2 = r.lift(0);
  r.rider(0, 0);
  r.box({8, 15}, {2, 1}, 0xFFFFFFFFu);
  const EntityId roof = r.box({0, 26.01f}, {5, 1}, 0xFFFFFFFFu);  // lower over the rider than that
  const BlockedMove m = moveBlocked(r.world, lift2, {0, 20});
  EXPECT_EQ(m.hitY, roof);
}

TEST(Riding, CarriedBodiesDontClimbOutFromUnderTheirRiders) {
  Yard y;
  const EntityId root = y.world.createEntity();
  y.world.addComponent<TransformComponent>(root);
  y.world.addComponent<TerrainComponent>(root).chains.emplace_back(std::vector<glm::vec2>{{-10, 0}, {10, 0}}, false, false);
  const EntityId fixed = y.world.createEntity();
  y.world.addComponent<TransformComponent>(fixed);
  auto& chains = y.world.addComponent<TerrainComponent>(fixed).chains;
  chains.emplace_back(std::vector<glm::vec2>{{1, 0.5f}, {3, 0.5f}}, false, false);  // a ledge
  chains.emplace_back(std::vector<glm::vec2>{{-10, 2.1f}, {10, 2.1f}}, false, false);
  const EntityId crate = y.crate({0, 0.51f}, {0.5f, 0.5f});
  const EntityId top = y.box({0, 1.52f}, {0.5f, 0.5f});
  moveBlocked(y.world, root, {2, 0});
  EXPECT_GE(y.at(top).y - 0.5f, y.at(crate).y + 0.5f - 1e-3f);
}

TEST(Riding, CousinsGoFrontFirstToo) {
  Yard y;
  const EntityId root = y.world.createEntity();
  y.world.addComponent<TransformComponent>(root);
  auto& chains = y.world.addComponent<TerrainComponent>(root).chains;
  chains.emplace_back(std::vector<glm::vec2>{{-5, 0}, {10, 0}}, false, false);
  chains.emplace_back(std::vector<glm::vec2>{{-0.9f, 4}, {0.9f, 4}}, false, false);  // a shelf
  const EntityId low = y.crate({2, 0.51f}, {6, 0.5f});
  const EntityId shelved = y.crate({0, 4.51f}, {1, 0.5f});
  const EntityId tall = y.crate({-2.01f, 3.52f}, {0.99f, 2.5f});  // on `low`, behind `shelved`
  moveBlocked(y.world, root, {0.5f, 0});
  EXPECT_NEAR(y.at(low).x, 2.5f, 1e-4f);
  EXPECT_NEAR(y.at(shelved).x, 0.5f, 1e-4f);
  EXPECT_NEAR(y.at(tall).x, -1.51f, 1e-4f);
}

TEST(Riding, ALiftLeavesStaticSolidsWhereTheyAre) {
  Yard y;
  const EntityId lift = y.lift(0);
  const EntityId ceiling = y.box({0, 30}, {50, 5}, 0xFFFFFFFFu);
  moveBlocked(y.world, lift, {0, 40});  // up against it
  moveBlocked(y.world, lift, {0, 10});
  moveBlocked(y.world, lift, {10, 0});
  EXPECT_EQ(y.at(ceiling), glm::vec2(0, 30));
}

TEST(Riding, ACartWhoseRidersCantRiseDoesntClimbIntoTheSlope) {
  Yard y;
  const EntityId hill = y.world.createEntity();
  y.world.addComponent<TransformComponent>(hill);
  auto& chains = y.world.addComponent<TerrainComponent>(hill).chains;
  chains.emplace_back(std::vector<glm::vec2>{{-20, -20}, {20, 20}}, false, false);
  chains.emplace_back(std::vector<glm::vec2>{{-20, 5.03f}, {20, 5.03f}}, false, false);  // just over the rider
  const EntityId cart = y.box({0, 2.01f}, {1, 1}, 0xFFFFFFFFu);
  y.crate({0, 4.02f}, {1, 1});
  moveBlocked(y.world, cart, {1, 0});
  EXPECT_GE(y.at(cart).y - 1, y.at(cart).x + 1 - 0.02f);  // not into the slope under its leading corner
}

TEST(Riding, ARiderHeldUpByOneNotYetMovedCatchesUp) {
  Yard y;
  const EntityId root = y.world.createEntity();
  y.world.addComponent<TransformComponent>(root);
  auto& chains = y.world.addComponent<TerrainComponent>(root).chains;
  chains.emplace_back(std::vector<glm::vec2>{{-20, 0}, {20, 0}}, false, false);
  chains.emplace_back(std::vector<glm::vec2>{{-3, 4}, {-1, 4}}, false, false);  // a shelf
  const EntityId low = y.crate({-5, 0.51f}, {6, 0.5f});
  const EntityId front = y.crate({0, 3.52f}, {0.99f, 2.5f});  // on `low`
  const EntityId rear = y.crate({-2.01f, 4.51f}, {1, 0.5f});  // on the shelf, ahead of `low`
  moveBlocked(y.world, root, {0.5f, 0});
  EXPECT_NEAR(y.at(low).x, -4.5f, 1e-4f);
  EXPECT_NEAR(y.at(front).x, 0.5f, 1e-4f);
  EXPECT_NEAR(y.at(rear).x, -1.51f, 1e-4f);
}

TEST(Riding, FrontFirstGoesByWhereCollidersAre) {
  Yard y;
  const EntityId lift = y.box({0, -2}, {30, 2}, 0xFFFFFFFFu);
  const EntityId rear = y.crate({10, 1.01f}, {1, 1});
  y.world.getComponent<BoxColliderComponent>(rear)->offset.x = -12;  // at x=-2
  const EntityId front = y.crate({0, 1.01f}, {1, 1});
  y.world.getComponent<BoxColliderComponent>(front)->offset.x = 2;
  moveBlocked(y.world, lift, {4, 0});
  EXPECT_NEAR(y.at(rear).x, 14, 1e-4f);
  EXPECT_NEAR(y.at(front).x, 4, 1e-4f);
}

TEST(Riding, NothingOverARiderMeansNoCeiling) {
  Yard y;
  const EntityId lift = y.box({0, 0}, {1, 1}, 0xFFFFFFFFu);
  y.box({0, 2}, {1, 1});  // touching: its rise rounds differently
  const BlockedMove m = moveBlocked(y.world, lift, {0, 0.1f});
  EXPECT_EQ(m.hit.y, 0);
}

TEST(Riding, ACarrierIsNeverBlockedByWhatItCarries) {
  Yard y;
  const EntityId root = y.box({0.5f, 0.5f}, {0.5f, 0.5f}, 0xFFFFFFFFu);
  const EntityId shelf = y.crate({0.5f, 1.51f}, {0.5f, 0.5f});
  y.world.addComponent<TerrainComponent>(shelf).chains.emplace_back(std::vector<glm::vec2>{{1.5f, -0.51f}, {2.5f, -0.51f}}, false, false);
  const EntityId onShelf = y.crate({2.5f, 1.51f}, {0.5f, 0.5f});  // on the shelf's terrain, just ahead of it
  moveBlocked(y.world, root, {100, 0});
  EXPECT_NEAR(y.at(shelf).x, 100.5f, 1e-3f);
  EXPECT_NEAR(y.at(onShelf).x, 102.5f, 1e-3f);
}

TEST(Riding, ACarrierStopsBehindWhatItCarriesWhenAWallStopsThat) {
  Yard y;
  const EntityId root = y.box({0.5f, 0.5f}, {0.5f, 0.5f}, 0xFFFFFFFFu);
  const EntityId shelf = y.crate({0.5f, 1.51f}, {0.5f, 0.5f});
  y.world.addComponent<TerrainComponent>(shelf).chains.emplace_back(std::vector<glm::vec2>{{1.5f, -0.51f}, {2.5f, -0.51f}}, false, false);
  const EntityId onShelf = y.crate({2.5f, 1.51f}, {0.5f, 0.5f});
  y.box({4.5f, 2.05f}, {0.5f, 0.95f}, 0xFFFFFFFFu);  // a wall ahead of it
  moveBlocked(y.world, root, {2.5f, 0});
  EXPECT_NEAR(y.at(onShelf).x, 3.49f, 0.02f);
  EXPECT_LE(y.at(shelf).x + 0.5f, y.at(onShelf).x - 0.5f + 1e-3f);  // behind it, not in it
}

TEST(Riding, ARiderOnTwoLiftsMovedInOneFrameGoesOnce) {
  Yard y;
  const EntityId left = y.box({-12, -2}, {10, 2}, 0xFFFFFFFFu), right = y.box({12, -2}, {10, 2}, 0xFFFFFFFFu);
  const EntityId rider = y.rider(0, 0);  // across both
  CarryFrame frame;
  moveBlocked(y.world, left, {1, 0}, 0, false, &frame);
  moveBlocked(y.world, right, {1, 0}, 0, false, &frame);
  EXPECT_NEAR(y.at(rider).x, 1, 1e-4f);
  frame.carrier.clear();  // the next frame: either may carry it again
  moveBlocked(y.world, right, {0, 3}, 0, false, &frame);
  moveBlocked(y.world, left, {0, 3}, 0, false, &frame);
  EXPECT_NEAR(y.at(rider).y, 13.01f, 1e-3f);
}

TEST(Riding, APlatformThatDidntMoveLeavesItsRidersToAnother) {
  Yard y;
  const EntityId stuck = y.box({-12, -2}, {10, 2}, 0xFFFFFFFFu), free = y.box({12, -2}, {10, 2}, 0xFFFFFFFFu);
  y.box({-27, -2}, {5, 2}, 0xFFFFFFFFu);  // a wall right behind it
  const EntityId rider = y.rider(0, 0);
  CarryFrame frame;
  moveBlocked(y.world, stuck, {-4, 0}, 0, false, &frame);
  moveBlocked(y.world, free, {4, 0}, 0, false, &frame);
  EXPECT_NEAR(y.at(rider).x, 4, 1e-4f);
}
