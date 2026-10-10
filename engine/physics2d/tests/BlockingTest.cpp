#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <limits>
#include <random>

#include "Blocking.hpp"
#include "BoxColliderComponent.hpp"
#include "TransformComponent.hpp"

namespace {

constexpr uint32_t kPlayer = 1u << 0, kGhost = 1u << 1;

struct Scene {
  World world;

  Scene() {
    world.registerComponent<TransformComponent>();
    world.registerComponent<BoxColliderComponent>();
  }

  EntityId box(float x, float y, glm::vec2 half, uint32_t layer = kPlayer, uint32_t blocks = 0) {
    const EntityId id = world.createEntity();
    world.addComponent<TransformComponent>(id).position = {x, y, 0.0f};
    auto& c = world.addComponent<BoxColliderComponent>(id);
    c.halfExtents = half;
    c.collisionLayer = layer;
    c.blocksMask = blocks;
    return id;
  }
  EntityId wall(float x, float y, glm::vec2 half, uint32_t blocks = 0xFFFFFFFFu) { return box(x, y, half, 1u << 31, blocks); }
  glm::vec2 at(EntityId id) { return glm::vec2(world.getComponent<TransformComponent>(id)->position); }
};

}  // namespace

TEST(Blocking, StopsFlushAgainstASolid) {
  Scene s;
  const EntityId player = s.box(0, 0, {8, 8});
  const EntityId wall = s.wall(40, 0, {8, 16});
  const BlockedMove m = moveBlocked(s.world, player, {100, 0});
  EXPECT_NEAR(s.at(player).x, 24.0f, 0.02f);  // its right edge at the wall's left (32)
  EXPECT_LT(s.at(player).x, 24.0f);           // not touching: no overlap reported next frame
  EXPECT_EQ(m.hit, glm::ivec2(1, 0));
  EXPECT_EQ(m.hitX, wall);
  EXPECT_EQ(m.hitY, kNoEntityId);
}

TEST(Blocking, AnOpenMoveGoesAllTheWay) {
  Scene s;
  const EntityId player = s.box(0, 0, {8, 8});
  s.wall(40, 40, {8, 8});  // above the path
  const BlockedMove m = moveBlocked(s.world, player, {100, -5});
  EXPECT_EQ(s.at(player), glm::vec2(100, -5));
  EXPECT_EQ(m.hit, glm::ivec2(0, 0));
}

// A fast move can't skip a thin wall: the test is exact, not stepped.
TEST(Blocking, AFastMoveDoesntTunnel) {
  Scene s;
  const EntityId player = s.box(0, 0, {4, 4});
  s.wall(50, 0, {0.5f, 50});
  moveBlocked(s.world, player, {10000, 0});
  EXPECT_LT(s.at(player).x, 50.0f);
}

// Blocked along y (falling onto the floor), still moving along x.
TEST(Blocking, LandingSlidesAlongTheFloor) {
  Scene s;
  const EntityId player = s.box(0, 20, {8, 8});
  const EntityId floor = s.wall(0, -8, {200, 8});
  const BlockedMove m = moveBlocked(s.world, player, {5, -30});
  EXPECT_FLOAT_EQ(s.at(player).x, 5.0f);
  EXPECT_NEAR(s.at(player).y, 8.0f, 0.02f);
  EXPECT_EQ(m.hit, glm::ivec2(0, -1));
  EXPECT_EQ(m.hitY, floor);
}

// The nearest blocker stops it, whatever order the world keeps them in.
TEST(Blocking, TheNearestBlockerStopsIt) {
  Scene s;
  const EntityId player = s.box(0, 0, {8, 8});
  s.wall(80, 0, {8, 8});
  const EntityId near = s.wall(40, 0, {8, 8});
  EXPECT_EQ(moveBlocked(s.world, player, {100, 0}).hitX, near);
  EXPECT_EQ(moveBlocked(s.world, player, {-100, 0}).hitX, kNoEntityId);
}

TEST(Blocking, OnlyLayersInTheBlocksMaskAreStopped) {
  Scene s;
  const EntityId ghost = s.box(0, 0, {8, 8}, kGhost);
  const EntityId player = s.box(0, 100, {8, 8}, kPlayer);
  s.wall(40, 0, {8, 8}, kPlayer);
  s.wall(40, 100, {8, 8}, kPlayer);
  moveBlocked(s.world, ghost, {100, 0});
  moveBlocked(s.world, player, {100, 0});
  EXPECT_EQ(s.at(ghost).x, 100.0f);
  EXPECT_LT(s.at(player).x, 24.1f);
}

// Two solid movers block each other, and neither blocks itself.
TEST(Blocking, SolidMoversBlockEachOther) {
  Scene s;
  const EntityId a = s.box(0, 0, {8, 8}, kPlayer, kPlayer);
  const EntityId b = s.box(40, 0, {8, 8}, kPlayer, kPlayer);
  EXPECT_EQ(moveBlocked(s.world, a, {100, 0}).hitX, b);
  EXPECT_EQ(moveBlocked(s.world, b, {-100, 0}).hitX, a);
  EXPECT_GT(s.at(b).x - s.at(a).x, 16.0f);
}

// Something that starts inside a solid (spawned there, or the solid appeared
// around it) can walk out instead of being stuck.
TEST(Blocking, StartingInsideASolidIsFreeToLeave) {
  Scene s;
  const EntityId player = s.box(0, 0, {8, 8});
  s.wall(4, 0, {8, 8});
  EXPECT_EQ(moveBlocked(s.world, player, {-50, 0}).hit, glm::ivec2(0, 0));
  EXPECT_EQ(s.at(player).x, -50.0f);
}

// Blocked by a corner, slide nudges it sideways into the gap.
TEST(Blocking, SlideNudgesIntoAGap) {
  Scene s;
  const EntityId player = s.box(0, 0, {8, 8});
  s.wall(-14, 30, {8, 8});  // a doorway from x = -6 to 10, just the player's width, 2 to its right
  s.wall(18, 30, {8, 8});
  const BlockedMove m = moveBlocked(s.world, player, {0, 20}, 3);
  EXPECT_EQ(m.hit.y, 1);
  EXPECT_FLOAT_EQ(s.at(player).x, 2.0f);  // lined up with the gap
  for (int i = 0; i < 4; ++i) moveBlocked(s.world, player, {0, 20}, 3);
  EXPECT_GT(s.at(player).y, 40.0f);  // through
}

TEST(Blocking, TheColliderOffsetCounts) {
  Scene s;
  const EntityId player = s.box(0, 0, {8, 8});
  s.world.getComponent<BoxColliderComponent>(player)->offset = {10, 0};
  s.wall(40, 0, {8, 8});
  moveBlocked(s.world, player, {100, 0});
  EXPECT_NEAR(s.at(player).x, 14.0f, 0.02f);
}

TEST(Blocking, NonsenseMovesChangeNothing) {
  Scene s;
  const EntityId player = s.box(0, 0, {8, 8});
  const float nan = std::numeric_limits<float>::quiet_NaN();
  moveBlocked(s.world, player, {nan, 5});
  moveBlocked(s.world, player, {5, 5}, std::numeric_limits<float>::infinity());
  EXPECT_EQ(s.at(player), glm::vec2(0, 0));
  moveBlocked(s.world, EntityId{12345, 0}, {1, 1});  // no such entity: nothing happens
}

// Not a test: what a move() costs among n colliders, half of them solid (a
// level's walls and crates). Run with --gtest_also_run_disabled_tests;
// scripts/bench.py reads the BENCH lines.
TEST(Blocking, DISABLED_MoveCost) {
  for (int count : {100, 2000}) {
    Scene s;
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> pos(0.0f, 2000.0f), step(-4.0f, 4.0f);
    for (int i = 0; i < count; ++i) s.box(pos(rng), pos(rng), {8, 8}, kPlayer, i % 2 ? 0xFFFFFFFFu : 0u);
    const EntityId mover = s.box(1000, 1000, {8, 8}, kPlayer);
    constexpr int kMoves = 10000;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kMoves; ++i) moveBlocked(s.world, mover, {step(rng), step(rng)}, 2);
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / kMoves;
    std::printf("BENCH move_among_%d_colliders %.3f us\n", count, us);
  }
}
