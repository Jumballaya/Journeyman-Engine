#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <limits>
#include <random>
#include <utility>
#include <vector>

#include "Systems.hpp"
#include "Terrain.hpp"

namespace {

using Pair = std::pair<EntityId, EntityId>;

struct Physics {
  World world;
  MoveFrame moves;  // shared like Physics2DModule's
  std::vector<Pair> collisions;
  std::vector<std::pair<ScriptEvent, Pair>> events;  // all but Overlap, in order

  Physics() {
    world.registerComponent<TransformComponent>();
    world.registerComponent<VelocityComponent>();
    world.registerComponent<LifetimeComponent>();
    world.registerComponent<ScrollWrapComponent>();
    world.registerComponent<BoxColliderComponent>();
    world.registerComponent<CircleColliderComponent>();
    world.registerComponent<GroundComponent>();
    world.registerSystem<MovementSystem>(&moves, [this](EntityId body, ScriptEvent e) { events.emplace_back(e, Pair(body, kNoEntityId)); });
    world.registerSystem<LifetimeSystem>();
    world.registerSystem<ScrollWrapSystem>();
    world.registerSystem<CollisionSystem>(
        [this](EntityId a, EntityId b, ScriptEvent e) {
          if (e == ScriptEvent::Overlap) collisions.emplace_back(a, b);
          else events.emplace_back(e, Pair(a, b));
        },
        &moves);
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
    c.collisionLayer = layer;
    c.collisionMask = wants;
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
    moves.clear();
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

TEST(Physics, AnOverlapStartsOnceAndEndsTheFrameAfterItStops) {
  Physics p;
  const EntityId a = p.mover(0, 0, 5), b = p.box(8, 0, 5);
  const Pair pair = p.inWorldOrder(a, b);
  p.frame();
  p.frame();
  ASSERT_EQ(p.events.size(), 1u);
  EXPECT_EQ(p.events[0], std::pair(ScriptEvent::OverlapStart, pair));
  p.world.getComponent<TransformComponent>(a)->position.x = -50;
  p.frame(0.0f);  // paused: nothing ends
  EXPECT_EQ(p.events.size(), 1u);
  p.frame();
  ASSERT_EQ(p.events.size(), 2u);
  EXPECT_EQ(p.events[1].first, ScriptEvent::OverlapEnd);
}

TEST(Physics, AnOverlapEndsWhenOneOfThemIsGone) {
  Physics p;
  const EntityId a = p.mover(0, 0, 5), b = p.box(8, 0, 5);
  p.frame();
  p.world.destroyEntity(b);
  p.frame();
  ASSERT_EQ(p.events.size(), 2u);
  EXPECT_EQ(p.events[1].first, ScriptEvent::OverlapEnd);
  EXPECT_TRUE(p.events[1].second.first == a || p.events[1].second.second == a);
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
      const bool interested = (ca->collisionLayer & cb->collisionMask) || (cb->collisionLayer & ca->collisionMask);
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

// However many of their shapes meet, two entities touch once a frame.
TEST(Collision, AnEntityWithABoxAndACircleIsReportedOnce) {
  Physics p;
  const EntityId other = p.box(0, 0, 10);
  const EntityId both = p.mover(5, 0, 4);
  p.world.addComponent<CircleColliderComponent>(both).radius = 4;
  p.frame();
  EXPECT_EQ(p.collisions, (std::vector<Pair>{Pair(other, both)}));
}

// A script moving a collider by its offset alone moves it: it collides.
TEST(Collision, MovingAColliderByItsOffsetCounts) {
  Physics p;
  const EntityId a = p.box(0, 0, 5);
  p.box(30, 0, 5);
  p.frame();
  p.world.getComponent<BoxColliderComponent>(a)->offset = {25, 0};
  p.frame();
  EXPECT_EQ(p.collisions.size(), 1u);
}

// A collider added to a still entity hasn't moved it: still pairs stay quiet.
TEST(Collision, AddingAColliderIsntMoving) {
  Physics p;
  const EntityId a = p.box(100, 0, 1);
  p.box(101, 0, 1);
  p.frame();
  p.world.addComponent<CircleColliderComponent>(a).radius = 1;
  p.frame();
  p.frame();
  EXPECT_TRUE(p.collisions.empty());
}

TEST(Physics, AWalkingVelocityLandsAndStaysOnTheGround) {
  Physics p;
  const EntityId ground = p.at(0, 0);
  p.world.addComponent<GroundComponent>(ground).chains.emplace_back(std::vector<glm::vec2>{{-100, 0}, {100, 0}}, false, false);
  const EntityId body = p.mover(0, 20, 5);
  auto& v = *p.world.getComponent<VelocityComponent>(body);
  v.acceleration = {0, -900};
  v.motion = kWalkMotion;
  for (int i = 0; i < 120; ++i) p.frame();
  EXPECT_NEAR(p.position(body).y, 5.01f, 0.02f);
  EXPECT_EQ(v.blocked.y, -1);
  EXPECT_GE(v.velocity.y, -900.0f / 60.0f - 1e-3f);  // stopped each step, not piling up
}

TEST(Physics, AWalkerReportsLandingAndLeavingTheGroundOnce) {
  Physics p;
  const EntityId ground = p.at(0, 0);
  p.world.addComponent<GroundComponent>(ground).chains.emplace_back(std::vector<glm::vec2>{{-100, 0}, {100, 0}}, false, false);
  const EntityId body = p.mover(0, 20, 5);
  auto& v = *p.world.getComponent<VelocityComponent>(body);
  v.acceleration = {0, -900};
  v.motion = kWalkMotion;
  for (int i = 0; i < 60; ++i) p.frame();
  p.frame(0.0f);  // paused: still on the ground
  using Ground = std::pair<ScriptEvent, Pair>;
  ASSERT_EQ(p.events, std::vector<Ground>{Ground(ScriptEvent::Landed, Pair(body, kNoEntityId))});
  v.velocity.y = 300;  // jump
  p.frame();
  ASSERT_EQ(p.events.size(), 2u);
  EXPECT_EQ(p.events[1], Ground(ScriptEvent::LeftGround, Pair(body, kNoEntityId)));
}

TEST(Physics, AMovingVelocityStopsAtAWallAndLosesThatSpeed) {
  Physics p;
  p.world.getComponent<BoxColliderComponent>(p.box(30, 0, 5))->blocksMask = 0xFFFFFFFFu;
  const EntityId body = p.mover(0, 0, 5, {600, 120});
  auto& v = *p.world.getComponent<VelocityComponent>(body);
  v.motion = kMoveMotion;
  p.frame();
  p.frame();  // reaches it
  EXPECT_EQ(v.blocked.x, 1);
  for (int i = 0; i < 8; ++i) p.frame();
  EXPECT_NEAR(p.position(body).x, 20, 0.02f);
  EXPECT_EQ(v.velocity.x, 0);
  EXPECT_EQ(v.velocity.y, 120);  // still free that way
}

TEST(Physics, ABlockedVelocityIsSweptAsFarAsItWent) {
  Physics p;
  p.world.getComponent<BoxColliderComponent>(p.box(10, 0, 1, 2, 0))->blocksMask = 0xFFFFFFFFu;  // a wall
  const EntityId gate = p.at(4, 0);  // a thin trigger it passes on the way, in one frame
  auto& c = p.world.addComponent<BoxColliderComponent>(gate);
  c.halfExtents = {0.2f, 1};
  c.collisionLayer = 4;
  const EntityId body = p.mover(0, 0, 1, {1200, 0});
  p.world.getComponent<VelocityComponent>(body)->motion = kMoveMotion;
  p.frame();
  EXPECT_NEAR(p.position(body).x, 7.99f, 0.02f);  // stopped at the wall, its velocity gone
  ASSERT_EQ(p.collisions.size(), 1u);
  EXPECT_EQ(p.collisions[0], p.inWorldOrder(body, gate));
}

TEST(Physics, ARiderOnAVelocityLiftEndsTheSameWhicheverWasMadeFirst) {
  glm::vec3 at[2];
  for (const bool riderFirst : {false, true}) {
    Physics p;
    EntityId rider = kNoEntityId, lift = kNoEntityId;
    const auto makeRider = [&] {
      rider = p.mover(5.5f, 2.01f, 1, {40, 0});
      auto& v = *p.world.getComponent<VelocityComponent>(rider);
      v.acceleration = {0, -100};
      v.motion = kWalkMotion;
    };
    if (riderFirst) makeRider();
    lift = p.mover(0, 0, 1, {80, 0});
    p.world.getComponent<BoxColliderComponent>(lift)->halfExtents = {5, 1};  // it walks off the end
    p.world.getComponent<BoxColliderComponent>(lift)->blocksMask = 0xFFFFFFFFu;
    p.world.getComponent<VelocityComponent>(lift)->motion = kMoveMotion;
    if (!riderFirst) makeRider();
    p.frame(0.05f);
    at[riderFirst] = p.position(rider);
    const bool beside = at[riderFirst].x - 1 >= p.position(lift).x + 5, above = at[riderFirst].y - 1 >= p.position(lift).y + 1;
    EXPECT_TRUE(beside || above);  // not in it
  }
  EXPECT_NEAR(at[0].x, at[1].x, 1e-4f);
  EXPECT_NEAR(at[0].y, at[1].y, 1e-4f);
}

TEST(Physics, ACarriedBodyIsSweptAlongWhereItWasCarried) {
  Physics p;
  const EntityId lift = p.mover(0, 0, 1, {1200, 0});
  p.world.getComponent<BoxColliderComponent>(lift)->halfExtents = {3, 1};
  p.world.getComponent<BoxColliderComponent>(lift)->blocksMask = 0xFFFFFFFFu;
  p.world.getComponent<BoxColliderComponent>(lift)->collisionMask = 0;
  p.world.getComponent<VelocityComponent>(lift)->motion = kMoveMotion;
  const EntityId rider = p.mover(0, 2.01f, 1);  // standing still on it
  const EntityId gate = p.box(10, 2, 0.2f, 4);  // a thin trigger at its height, passed in one frame
  p.frame();
  EXPECT_NEAR(p.position(rider).x, 20, 1e-3f);
  ASSERT_EQ(p.collisions.size(), 1u);
  EXPECT_EQ(p.collisions[0], p.inWorldOrder(rider, gate));
}

TEST(Physics, VelocityDrivenTerrainCarriesWhatStandsOnIt) {
  Physics p;
  const EntityId ground = p.at(0, 0);
  p.world.addComponent<GroundComponent>(ground).chains.emplace_back(std::vector<glm::vec2>{{-50, 0}, {50, 0}}, false, false);
  auto& v = p.world.addComponent<VelocityComponent>(ground);
  v.velocity = {0, 60};
  v.motion = kMoveMotion;
  const EntityId rider = p.box(0, 5.01f, 5);
  for (int i = 0; i < 10; ++i) p.frame();
  EXPECT_NEAR(p.position(ground).y, 10, 1e-3f);
  EXPECT_NEAR(p.position(rider).y, 15.01f, 1e-2f);
}

TEST(Physics, ACarrierWithABoxAndLowerTerrainStillGoesFirst) {
  Physics p;
  const EntityId rider = p.mover(5.5f, 1.01f, 1, {40, 0});
  auto& rv = *p.world.getComponent<VelocityComponent>(rider);
  rv.acceleration = {0, -100};
  rv.motion = kWalkMotion;
  const EntityId carrier = p.mover(0, 10, 1, {80, 0});  // a box up top, a shelf hanging under it
  p.world.addComponent<GroundComponent>(carrier).chains.emplace_back(std::vector<glm::vec2>{{-5, -10}, {5, -10}}, false, false);
  p.world.getComponent<VelocityComponent>(carrier)->motion = kMoveMotion;
  p.frame(0.05f);
  EXPECT_NEAR(p.position(rider).x, 11.5f, 1e-3f);  // carried 4, then walked 2 off the shelf's end
}

TEST(Physics, AWalkerKnowsWhatItStandsOnAndHowFastThatGoes) {
  Physics p;
  const EntityId lift = p.mover(0, 0, 1, {30, 60});
  p.world.getComponent<BoxColliderComponent>(lift)->halfExtents = {20, 1};
  p.world.getComponent<BoxColliderComponent>(lift)->blocksMask = 0xFFFFFFFFu;
  p.world.getComponent<VelocityComponent>(lift)->motion = kMoveMotion;
  const EntityId walker = p.mover(0, 2.01f, 1);
  auto& v = *p.world.getComponent<VelocityComponent>(walker);
  v.acceleration = {0, -900};
  v.motion = kWalkMotion;
  for (int i = 0; i < 5; ++i) p.frame();
  EXPECT_EQ(v.floor, lift);
  EXPECT_NEAR(v.platformVelocity.x, 30, 0.01f);
  EXPECT_NEAR(v.platformVelocity.y, 60, 0.01f);
  v.velocity.y = 600;  // jumps off
  p.frame();
  EXPECT_EQ(v.floor, kNoEntityId);
  EXPECT_EQ(v.platformVelocity, glm::vec2(0.0f));
}

TEST(Physics, FloorIsWhatABodyStandsOnAfterEverythingMoved) {
  Physics p;
  const EntityId floor = p.box(0, 0, 1);
  p.world.getComponent<BoxColliderComponent>(floor)->blocksMask = 0xFFFFFFFFu;
  const EntityId body = p.mover(0, 2.01f, 1);
  const EntityId pusher = p.mover(-3, 3, 1, {180, 0});  // listed after the body, so it moves after it
  p.world.getComponent<BoxColliderComponent>(pusher)->blocksMask = 0xFFFFFFFFu;
  p.world.getComponent<VelocityComponent>(pusher)->motion = kMoveMotion;
  auto& v = *p.world.getComponent<VelocityComponent>(body);
  v.acceleration = {0, -100};
  v.motion = kWalkMotion;
  p.frame();
  EXPECT_GT(p.position(body).x, 1);  // pushed off the floor
  EXPECT_EQ(v.floor, kNoEntityId);

  v.floor = floor;  // and a body that stops being moved by blocking forgets it all
  v.platformVelocity = {5, 5};
  v.motion = kFreeMotion;
  p.frame();
  EXPECT_EQ(v.floor, kNoEntityId);
  EXPECT_EQ(v.platformVelocity, glm::vec2(0.0f));
}

// Landing and leaving go by where a body ends up after every mover, not its own sweep.
TEST(Physics, GroundEventsFollowTheFloorAfterEverythingMoved) {
  using Ground = std::pair<ScriptEvent, Pair>;
  for (const bool settled : {false, true}) {
    Physics p;
    const EntityId floor = p.box(0, 0, 1);
    p.world.getComponent<BoxColliderComponent>(floor)->blocksMask = 0xFFFFFFFFu;
    const EntityId body = p.mover(0, 2.01f, 1);
    const EntityId pusher = p.mover(-3, 3, 1);  // listed after the body, so it moves after it
    p.world.getComponent<BoxColliderComponent>(pusher)->blocksMask = 0xFFFFFFFFu;
    p.world.getComponent<VelocityComponent>(pusher)->motion = kMoveMotion;
    auto& v = *p.world.getComponent<VelocityComponent>(body);
    v.acceleration = {0, -100};
    v.motion = kWalkMotion;
    if (settled) {
      for (int i = 0; i < 10; ++i) p.frame();
      ASSERT_EQ(p.events, std::vector<Ground>{Ground(ScriptEvent::Landed, Pair(body, kNoEntityId))});
      p.events.clear();
    }
    p.world.getComponent<VelocityComponent>(pusher)->velocity = {180, 0};
    p.frame();
    ASSERT_GT(p.position(body).x, 1);  // pushed off the floor
    EXPECT_EQ(p.events, settled ? std::vector<Ground>{Ground(ScriptEvent::LeftGround, Pair(body, kNoEntityId))}
                                : std::vector<Ground>{});
  }
}

TEST(Physics, ABodyPushedAlongTheFloorStillStandsOnIt) {
  Physics p;
  const EntityId floor = p.box(0, 0, 1);
  p.world.getComponent<BoxColliderComponent>(floor)->halfExtents = {20, 1};
  p.world.getComponent<BoxColliderComponent>(floor)->blocksMask = 0xFFFFFFFFu;
  const EntityId body = p.mover(0, 2.01f, 1);
  const EntityId pusher = p.mover(-3, 3, 1, {180, 0});
  p.world.getComponent<BoxColliderComponent>(pusher)->blocksMask = 0xFFFFFFFFu;
  p.world.getComponent<VelocityComponent>(pusher)->motion = kMoveMotion;
  auto& v = *p.world.getComponent<VelocityComponent>(body);
  v.acceleration = {0, -100};
  v.motion = kWalkMotion;
  p.frame();
  EXPECT_GT(p.position(body).x, 1);
  EXPECT_EQ(v.floor, floor);
}

TEST(Physics, AWalkerIsSweptOverTheHillItClimbed) {
  Physics p;
  const EntityId ground = p.at(0, 0);
  p.world.addComponent<GroundComponent>(ground).chains.emplace_back(
      std::vector<glm::vec2>{{-100, 0}, {0, 0}, {10, 10}, {20, 0}, {100, 0}}, false, false);
  const EntityId walker = p.mover(-1, 0.21f, 0.2f, {440, 0});
  p.world.getComponent<VelocityComponent>(walker)->motion = kWalkMotion;
  const EntityId flag = p.box(10, 10.21f, 0.2f, 4);  // on the crest: passed only by going over it
  p.frame(0.05f);
  EXPECT_NEAR(p.position(walker).x, 21, 1e-3f);
  ASSERT_EQ(p.collisions.size(), 1u);
  EXPECT_EQ(p.collisions[0], p.inWorldOrder(walker, flag));
}

TEST(Physics, ARiderWithLowerGroundOfItsOwnStillGoesAfterItsLift) {
  Physics p;
  const EntityId rider = p.mover(5.5f, 2.01f, 1, {40, 0});  // listed first, and lowest
  auto& v = *p.world.getComponent<VelocityComponent>(rider);
  v.acceleration = {0, -100};
  v.motion = kWalkMotion;
  const EntityId lift = p.mover(0, 0, 1, {80, 0});
  p.world.getComponent<BoxColliderComponent>(lift)->halfExtents = {5, 1};
  p.world.getComponent<BoxColliderComponent>(lift)->blocksMask = 0xFFFFFFFFu;
  p.world.getComponent<VelocityComponent>(lift)->motion = kMoveMotion;
  p.world.addComponent<GroundComponent>(rider).chains.emplace_back(std::vector<glm::vec2>{{30, -20}, {40, -20}}, false, false);
  p.frame(0.05f);
  EXPECT_NEAR(p.position(rider).x, 11.5f, 1e-3f);  // carried 4, then walked 2
}

TEST(Physics, AScriptsMoveIsSweptButNotATeleport) {
  Physics p;
  const EntityId body = p.box(0, 0, 0.5f);
  const EntityId gate = p.box(10, 0, 0.2f, 4);
  moveBlocked(p.world, body, {20, 0}, 0.0f, &p.moves);  // a script's move() before its first frame, through the gate
  p.frame();
  ASSERT_EQ(p.collisions.size(), 1u);
  EXPECT_EQ(p.collisions[0], p.inWorldOrder(body, gate));
  p.world.getComponent<TransformComponent>(body)->position.x = -20;  // set: jumps back over it
  p.frame();
  EXPECT_TRUE(p.collisions.empty());
}

TEST(Physics, AMoveIsSweptAcrossThenUp) {
  Physics p;
  const EntityId body = p.mover(0, 0, 0.1f, {30, 30});
  p.world.getComponent<VelocityComponent>(body)->motion = kMoveMotion;
  const EntityId corner = p.box(0.5f, 0, 0.05f, 4);  // where it turned
  p.frame();
  ASSERT_EQ(p.collisions.size(), 1u);
  EXPECT_EQ(p.collisions[0], p.inWorldOrder(body, corner));
}

TEST(Physics, ALiftHeldDownByItsRiderIsntSweptWhereItDidntGo) {
  Physics p;
  const EntityId lift = p.mover(0, 0, 1, {0, 600});  // 10 up a frame
  p.world.getComponent<BoxColliderComponent>(lift)->halfExtents = {5, 1};
  p.world.getComponent<BoxColliderComponent>(lift)->blocksMask = 0xFFFFFFFFu;
  p.world.getComponent<VelocityComponent>(lift)->motion = kMoveMotion;
  p.box(0, 2.01f, 1);  // its rider, 1 under a ceiling
  p.world.getComponent<BoxColliderComponent>(p.box(0, 5.02f, 1))->blocksMask = 0xFFFFFFFFu;
  p.box(4, 6, 0.5f, 4);  // where it would have risen to
  p.frame();
  EXPECT_LT(p.position(lift).y, 1.1f);
  for (const Pair& c : p.collisions) EXPECT_TRUE(c.first != lift && c.second != lift);
}

TEST(Physics, AFreeBodyCarriedAfterItMovedIsSweptAllTheWay) {
  Physics p;
  const EntityId rider = p.mover(0, 1.01f, 0.5f, {600, 0}, 1, 4);  // 10 a frame, freely
  const EntityId lift = p.mover(0, 0, 1, {600, 0});
  p.world.getComponent<BoxColliderComponent>(lift)->halfExtents = {50, 0.5f};
  p.world.getComponent<BoxColliderComponent>(lift)->blocksMask = 0xFFFFFFFFu;
  p.world.getComponent<BoxColliderComponent>(lift)->collisionMask = 0;
  p.world.getComponent<VelocityComponent>(lift)->motion = kMoveMotion;
  const EntityId gate = p.box(5, 1.01f, 0.2f, 4);  // passed before the carry
  p.frame();
  EXPECT_NEAR(p.position(rider).x, 20, 1e-3f);
  ASSERT_EQ(p.collisions.size(), 1u);
  EXPECT_EQ(p.collisions[0], p.inWorldOrder(rider, gate));
}

TEST(Physics, AWalkerOnACrateOnALiftGoesAfterTheLift) {
  Physics p;
  const EntityId walker = p.mover(1.5f, 4.02f, 1, {40, 0});  // listed first, on the crate's edge
  p.world.getComponent<VelocityComponent>(walker)->motion = kWalkMotion;
  const EntityId lift = p.mover(0, 0, 1, {80, 0});
  p.world.getComponent<BoxColliderComponent>(lift)->halfExtents = {2, 1};
  p.world.getComponent<BoxColliderComponent>(lift)->blocksMask = 0xFFFFFFFFu;
  p.world.getComponent<VelocityComponent>(lift)->motion = kMoveMotion;
  const EntityId crate = p.mover(0, 2.01f, 1);  // still, moving freely: carried, not moving on its own
  p.world.getComponent<BoxColliderComponent>(crate)->blocksMask = 0xFFFFFFFFu;
  p.frame(0.05f);
  EXPECT_NEAR(p.position(crate).x, 4, 1e-3f);
  EXPECT_NEAR(p.position(walker).x, 7.5f, 1e-3f);  // carried 4 with it, then walked 2
}

TEST(Physics, TwoVelocityLiftsCarryASharedRiderOnce) {
  Physics p;
  for (const float x : {-6.0f, 6.0f}) {
    const EntityId lift = p.mover(x, 0, 1, {60, 0});  // 1 a frame
    p.world.getComponent<BoxColliderComponent>(lift)->halfExtents = {5, 1};
    p.world.getComponent<BoxColliderComponent>(lift)->blocksMask = 0xFFFFFFFFu;
    p.world.getComponent<VelocityComponent>(lift)->motion = kMoveMotion;
  }
  const EntityId rider = p.box(0, 2.01f, 1);
  p.world.getComponent<BoxColliderComponent>(rider)->halfExtents = {2, 1};
  p.frame();
  EXPECT_NEAR(p.position(rider).x, 1, 1e-3f);
  p.frame();  // and again the next frame
  EXPECT_NEAR(p.position(rider).x, 2, 1e-3f);
}

TEST(Physics, AWalkerIsSweptOverTheHillGoingLeftToo) {
  Physics p;
  const EntityId ground = p.at(0, 0);
  p.world.addComponent<GroundComponent>(ground).chains.emplace_back(
      std::vector<glm::vec2>{{-100, 0}, {0, 0}, {10, 10}, {20, 0}, {100, 0}}, false, false);
  const EntityId walker = p.mover(21, 0.21f, 0.2f, {-440, 0});
  p.world.getComponent<VelocityComponent>(walker)->motion = kWalkMotion;
  const EntityId flag = p.box(10, 10.21f, 0.2f, 4);
  p.frame(0.05f);
  ASSERT_EQ(p.collisions.size(), 1u);
  EXPECT_EQ(p.collisions[0], p.inWorldOrder(walker, flag));
}

TEST(Physics, ABodyPushedOntoAnotherFloorStandsOnThatOne) {
  Physics p;
  const EntityId floor = p.box(0, 0, 1), next = p.box(4, 0, 1);
  for (const EntityId f : {floor, next}) p.world.getComponent<BoxColliderComponent>(f)->blocksMask = 0xFFFFFFFFu;
  const EntityId body = p.mover(0, 2.01f, 1);
  const EntityId pusher = p.mover(-3, 3, 1, {180, 0});
  p.world.getComponent<BoxColliderComponent>(pusher)->blocksMask = 0xFFFFFFFFu;
  p.world.getComponent<VelocityComponent>(pusher)->motion = kMoveMotion;
  auto& v = *p.world.getComponent<VelocityComponent>(body);
  v.acceleration = {0, -100};
  v.motion = kWalkMotion;
  p.frame();
  ASSERT_TRUE(standsOn(p.world, body, next));
  EXPECT_EQ(v.floor, next);
}

TEST(Physics, ADroppingWalkerPushedOverAOneWayDoesntStandOnIt) {
  Physics p;
  const EntityId shelf = p.at(0, 0);
  p.world.addComponent<GroundComponent>(shelf).chains.emplace_back(std::vector<glm::vec2>{{-10, 0}, {10, 0}}, false, true);
  const EntityId body = p.mover(0, 1.02f, 1);  // just over it, dropping through
  const EntityId pusher = p.mover(-3, 1.5f, 1, {180, 0});
  p.world.getComponent<BoxColliderComponent>(pusher)->blocksMask = 0xFFFFFFFFu;
  p.world.getComponent<VelocityComponent>(pusher)->motion = kMoveMotion;
  auto& v = *p.world.getComponent<VelocityComponent>(body);
  v.motion = kWalkMotion;
  v.dropThrough = 1;
  p.frame();
  EXPECT_GT(p.position(body).x, 0);  // pushed
  EXPECT_EQ(v.floor, kNoEntityId);
}

TEST(Physics, ABodyALiftLeavesBehindStandsOnNothing) {
  Physics p;
  const EntityId body = p.mover(0, 3, 1, {0, -60});  // landing on the lift this frame
  p.world.getComponent<BoxColliderComponent>(p.box(-1.5f, 3, 0.5f))->blocksMask = 0xFFFFFFFFu;  // a wall behind it
  const EntityId lift = p.mover(0, 0, 1, {-120, 0});  // then leaving to the left without it
  p.world.getComponent<BoxColliderComponent>(lift)->blocksMask = 0xFFFFFFFFu;
  p.world.getComponent<VelocityComponent>(lift)->motion = kMoveMotion;
  auto& v = *p.world.getComponent<VelocityComponent>(body);
  v.motion = kWalkMotion;
  p.frame();
  ASSERT_FALSE(standsOn(p.world, body, lift));
  EXPECT_EQ(v.floor, kNoEntityId);
  EXPECT_EQ(v.platformVelocity, glm::vec2(0.0f));
}
