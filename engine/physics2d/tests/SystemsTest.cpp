#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <limits>
#include <random>
#include <utility>
#include <vector>

#include "Systems.hpp"

namespace {

using Pair = std::pair<EntityId, EntityId>;

struct Physics {
  World world;
  std::vector<Pair> collisions;

  Physics() {
    world.registerComponent<TransformComponent>();
    world.registerComponent<VelocityComponent>();
    world.registerComponent<LifetimeComponent>();
    world.registerComponent<ScrollWrapComponent>();
    world.registerComponent<BoxColliderComponent>();
    world.registerComponent<CircleColliderComponent>();
    world.registerSystem<MovementSystem>();
    world.registerSystem<LifetimeSystem>();
    world.registerSystem<ScrollWrapSystem>();
    world.registerSystem<CollisionSystem>([this](EntityId a, EntityId b) { collisions.emplace_back(a, b); });
  }

  EntityId at(float x, float y) {
    const EntityId id = world.createEntity();
    world.addComponent<TransformComponent>(id).position = {x, y, 0.0f};
    return id;
  }
  EntityId box(float x, float y, float half, uint32_t layer = 1, uint32_t wants = 0xFFFFFFFFu) {
    const EntityId id = at(x, y);
    auto& c = world.addComponent<BoxColliderComponent>(id);
    c.halfExtents = {half, half};
    c.layerMask = layer;
    c.collidesWithMask = wants;
    return id;
  }
  EntityId mover(float x, float y, float half, glm::vec2 velocity = {}, uint32_t layer = 1, uint32_t wants = 0xFFFFFFFFu) {
    const EntityId id = box(x, y, half, layer, wants);
    world.addComponent<VelocityComponent>(id).velocity = velocity;
    return id;
  }
  glm::vec3 position(EntityId id) { return world.getComponent<TransformComponent>(id)->position; }
  // The pair as collisions report it: the one the world visits first, first.
  Pair inWorldOrder(EntityId a, EntityId b) {
    for (auto [id, t, c] : world.view<TransformComponent, BoxColliderComponent>()) {
      if (id == a) return {a, b};
      if (id == b) return {b, a};
    }
    return {a, b};
  }
  void frame(float dt = 1.0f / 60.0f) {
    collisions.clear();
    world.runSystems(dt);
  }
};

}  // namespace

TEST(Physics, SimulationStepClampsAndRejectsNonsense) {
  EXPECT_FLOAT_EQ(simulationStep(0.01f), 0.01f);
  EXPECT_FLOAT_EQ(simulationStep(1.0f), 1.0f / 20.0f);  // a hitch doesn't teleport
  EXPECT_FLOAT_EQ(simulationStep(-1.0f), 0.0f);
  EXPECT_FLOAT_EQ(simulationStep(std::numeric_limits<float>::quiet_NaN()), 0.0f);
  EXPECT_FLOAT_EQ(simulationStep(std::numeric_limits<float>::infinity()), 0.0f);
}

TEST(Physics, MovementIntegratesAccelerationThenVelocity) {
  Physics p;
  const EntityId e = p.at(0, 0);
  auto& v = p.world.addComponent<VelocityComponent>(e);
  v.velocity = {60, 0};
  v.acceleration = {0, -600};
  p.frame(0.05f);
  EXPECT_FLOAT_EQ(p.world.getComponent<VelocityComponent>(e)->velocity.y, -30.0f);
  EXPECT_FLOAT_EQ(p.position(e).x, 3.0f);
  EXPECT_FLOAT_EQ(p.position(e).y, -1.5f);  // the new velocity moves it this frame
}

TEST(Physics, ANonsenseFrameMovesNothing) {
  Physics p;
  const EntityId e = p.at(5, 5);
  p.world.addComponent<VelocityComponent>(e).velocity = {100, 100};
  p.frame(std::numeric_limits<float>::quiet_NaN());
  EXPECT_EQ(p.position(e), glm::vec3(5, 5, 0));
}

TEST(Physics, LifetimesExpireWithMovementsStep) {
  Physics p;
  const EntityId e = p.at(0, 0);
  p.world.addComponent<LifetimeComponent>(e).seconds = 0.1f;
  p.frame(1.0f);  // clamped to 1/20 s: still alive, as a bullet would still be flying
  EXPECT_FALSE(p.world.isPendingDestroy(e));
  p.frame(1.0f);
  EXPECT_TRUE(p.world.isPendingDestroy(e));
}

TEST(Physics, ScrollWrapKeepsYInItsBand) {
  Physics p;
  const EntityId e = p.at(0, 130);
  auto& wrap = p.world.addComponent<ScrollWrapComponent>(e);
  wrap.minY = 0;
  wrap.maxY = 100;
  p.frame();
  EXPECT_FLOAT_EQ(p.position(e).y, 30.0f);
  p.world.getComponent<TransformComponent>(e)->position.y = -250;
  p.frame();
  EXPECT_FLOAT_EQ(p.position(e).y, 50.0f);
}

TEST(Physics, OverlappingMoverReportsOncePerFrame) {
  Physics p;
  const EntityId a = p.mover(0, 0, 5), b = p.box(8, 0, 5);
  p.frame();
  ASSERT_EQ(p.collisions.size(), 1u);
  EXPECT_EQ(p.collisions[0], p.inWorldOrder(a, b));
  p.frame();
  EXPECT_EQ(p.collisions.size(), 1u);  // still overlapping: reported again
}

TEST(Physics, TouchingEdgesDontCollide) {
  Physics p;
  p.mover(0, 0, 5);
  p.box(10, 0, 5);
  p.frame();
  EXPECT_TRUE(p.collisions.empty());
}

TEST(Physics, TwoThatNeverMoveNeverCollide) {
  Physics p;
  p.box(0, 0, 5);
  p.box(3, 0, 5);
  p.frame();
  EXPECT_TRUE(p.collisions.empty());
}

TEST(Physics, ABodyMovedByAScriptCountsAsMoving) {
  Physics p;
  const EntityId a = p.box(0, 0, 5), b = p.box(30, 0, 5);
  p.frame();
  p.world.getComponent<TransformComponent>(a)->position.x = 25;  // a script moved it, no velocity
  p.frame();
  ASSERT_EQ(p.collisions.size(), 1u);
  EXPECT_EQ(p.collisions[0], p.inWorldOrder(a, b));
}

TEST(Physics, EitherSidesInterestIsEnough) {
  Physics p;
  // a wants nothing; b wants a's layer.
  const EntityId a = p.mover(0, 0, 5, {}, /*layer=*/2, /*wants=*/0);
  const EntityId b = p.box(4, 0, 5, /*layer=*/4, /*wants=*/2);
  p.frame();
  ASSERT_EQ(p.collisions.size(), 1u);
  EXPECT_EQ(p.collisions[0], p.inWorldOrder(a, b));

  Physics q;  // neither wants the other
  q.mover(0, 0, 5, {}, 2, 8);
  q.box(4, 0, 5, 4, 8);
  q.frame();
  EXPECT_TRUE(q.collisions.empty());
}

TEST(Physics, EntitiesBeingDestroyedDontCollide) {
  Physics p;
  const EntityId a = p.mover(0, 0, 5);
  p.box(4, 0, 5);
  p.world.destroyDeferred(a);
  p.frame();
  EXPECT_TRUE(p.collisions.empty());
}

TEST(Physics, PausedFramesReportNothing) {
  Physics p;
  p.mover(0, 0, 5);
  p.box(4, 0, 5);
  p.frame(0.0f);
  EXPECT_TRUE(p.collisions.empty());
}

// A fast bullet that starts short of a thin target and ends past it, within
// one frame, still hits it (it would tunnel through if only end positions counted).
TEST(Physics, AFastBodyCantPassThroughAThinOne) {
  Physics p;
  // 1/20 s at 780 px/s is 39 px; the target is 4 px wide, 20 px ahead.
  const EntityId bullet = p.mover(0, 0, 2, {780, 0});
  const EntityId target = p.box(20, 0, 2);
  p.frame(1.0f / 20.0f);
  EXPECT_GT(p.position(bullet).x, 24.0f) << "the bullet should end past the target";
  ASSERT_EQ(p.collisions.size(), 1u);
  EXPECT_EQ(p.collisions[0], p.inWorldOrder(bullet, target));
}

// Only velocity sweeps: a teleport (a respawn, a wrap) doesn't hit what lies between.
TEST(Physics, ATeleportDoesntSweep) {
  Physics p;
  const EntityId a = p.mover(0, 0, 2);
  p.box(50, 0, 2);
  p.frame();
  p.world.getComponent<TransformComponent>(a)->position.x = 100;
  p.frame();
  EXPECT_TRUE(p.collisions.empty());
}

TEST(Physics, BodiesMovingTogetherDontCollide) {
  Physics p;
  p.mover(0, 0, 2, {500, 0});
  p.mover(0, 10, 2, {500, 0});  // side by side, 10 apart, same velocity
  p.frame(1.0f / 20.0f);
  EXPECT_TRUE(p.collisions.empty());
}

// Paths that cross, but not at the same time, are no collision.
TEST(Physics, CrossingPathsAtDifferentTimesDontCollide) {
  Physics p;
  // a sweeps right along y=0 through x=50 early in the frame; b reaches y=0 at x=50 only at the end.
  p.mover(40, 0, 1, {400, 0});    // x 20 -> 40 during the frame (travel 20)
  p.mover(50, 0, 1, {0, 400});    // y -20 -> 0
  p.frame(1.0f / 20.0f);
  // At the end a is at x=40, b at (50, 0): 10 apart. Before, a at x=20, b at y=-20. In b's frame a moves
  // from (-30, 20) to (-10, 0), never within (2, 2) of the origin.
  EXPECT_TRUE(p.collisions.empty());
}

// The contract the scripts (and replays) rely on: each overlapping pair once,
// in the order the colliders sit in the world, the earlier entity first.
// Checked against every pair, on a crowded random scene.
TEST(Physics, ReportsEveryOverlapInWorldOrder) {
  Physics p;
  std::mt19937 rng(7);
  std::uniform_real_distribution<float> pos(0.0f, 400.0f), half(2.0f, 20.0f);
  std::vector<EntityId> all;
  for (int i = 0; i < 300; ++i) {
    const bool moves = i % 3 != 0;
    const uint32_t layer = 1u << (i % 4), wants = (i % 5 == 0) ? 0u : 0xFu;
    all.push_back(moves ? p.mover(pos(rng), pos(rng), half(rng), {}, layer, wants)
                        : p.box(pos(rng), pos(rng), half(rng), layer, wants));
  }
  p.frame();

  // Every pair, in the world's order.
  std::vector<Pair> expected;
  std::vector<EntityId> order;
  for (auto [entity, t, c] : p.world.view<TransformComponent, BoxColliderComponent>()) order.push_back(entity);
  for (size_t i = 0; i < order.size(); ++i) {
    for (size_t j = i + 1; j < order.size(); ++j) {
      const auto *ta = p.world.getComponent<TransformComponent>(order[i]), *tb = p.world.getComponent<TransformComponent>(order[j]);
      const auto *ca = p.world.getComponent<BoxColliderComponent>(order[i]), *cb = p.world.getComponent<BoxColliderComponent>(order[j]);
      const bool moves = p.world.hasComponent<VelocityComponent>(order[i]) || p.world.hasComponent<VelocityComponent>(order[j]);
      const bool interested = (ca->layerMask & cb->collidesWithMask) || (cb->layerMask & ca->collidesWithMask);
      const glm::vec2 d = glm::abs(glm::vec2(ta->position) - glm::vec2(tb->position));
      const glm::vec2 reach = ca->halfExtents + cb->halfExtents;
      if (moves && interested && d.x < reach.x && d.y < reach.y) expected.emplace_back(order[i], order[j]);
    }
  }
  ASSERT_GT(expected.size(), 20u) << "the scene should be crowded";
  EXPECT_EQ(p.collisions, expected);
}

// Not a test: how one collision frame scales with the number of colliders.
// Run with --gtest_also_run_disabled_tests --gtest_filter=*CollisionCost*.
TEST(Physics, DISABLED_CollisionCost) {
  for (int count : {100, 500, 2000, 5000}) {
    Physics p;
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> pos(0.0f, 2000.0f);
    for (int i = 0; i < count; ++i) p.mover(pos(rng), pos(rng), 8.0f);
    p.frame();
    const auto start = std::chrono::steady_clock::now();
    for (int f = 0; f < 20; ++f) p.frame();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 20;
    std::printf("BENCH collision_%d_colliders %.4f ms\n", count, ms);
  }
}

// A circle collides by its round shape: beside a box's corner it doesn't
// touch, though their bounding boxes overlap; moving, it can't skip past.
TEST(Collision, CirclesCollideByTheirShape) {
  Physics p;
  const EntityId box = p.box(0, 0, 10);
  const EntityId ball = p.at(14, 14);
  p.world.addComponent<CircleColliderComponent>(ball).radius = 5;
  p.world.addComponent<VelocityComponent>(ball);
  p.frame();
  EXPECT_TRUE(p.collisions.empty());
  p.world.getComponent<TransformComponent>(ball)->position = {13, 13, 0};
  p.frame();
  ASSERT_EQ(p.collisions.size(), 1u);
  EXPECT_EQ(p.collisions[0], Pair(box, ball));

  p.collisions.clear();
  p.world.getComponent<TransformComponent>(ball)->position = {500, 500, 0};
  const EntityId bullet = p.at(-200, 100);  // crosses a thin wall in one frame
  p.world.addComponent<CircleColliderComponent>(bullet).radius = 1;
  p.world.addComponent<VelocityComponent>(bullet).velocity = {6000, 0};
  const EntityId wall = p.box(0, 100, 1);
  p.frame(1.0f / 20.0f);
  ASSERT_EQ(p.collisions.size(), 1u);
  EXPECT_EQ(p.collisions[0], Pair(wall, bullet));
}
