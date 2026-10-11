#include <gtest/gtest.h>

#include "BoxColliderComponent.hpp"
#include "PathFollower.hpp"
#include "Systems.hpp"
#include "Terrain.hpp"
#include "TransformHierarchy.hpp"

namespace {

constexpr float kStep = 1.0f / 50.0f;  // under simulationStep's cap

struct Yard {
  World world;
  PathFollowerSystem follow;
  MovementSystem move;
  Yard() {
    world.registerComponent<TransformComponent>();
    world.registerComponent<BoxColliderComponent>();
    world.registerComponent<GroundComponent>();
    world.registerComponent<VelocityComponent>();
    world.registerComponent<PathFollowerComponent>();
  }
  EntityId follower(PathFollowerComponent c, glm::vec2 start = glm::vec2(0.0f)) {
    const EntityId id = world.createEntity();
    world.addComponent<TransformComponent>(id).position = {start, 0.0f};
    world.addComponent<PathFollowerComponent>(id, std::move(c));
    return id;
  }
  void tick(int frames = 1) {
    for (int i = 0; i < frames; ++i) {
      follow.update(world, kStep);
      move.update(world, kStep);
    }
  }
  glm::vec2 at(EntityId id) { return glm::vec2(world.getComponent<TransformComponent>(id)->position); }
};

PathFollowerComponent route(std::vector<glm::vec2> points, PathFollowerComponent::Mode mode, float speed = 50.0f) {
  PathFollowerComponent c;
  c.points = std::move(points);
  c.mode = mode;
  c.speed = speed;
  return c;
}

}  // namespace

TEST(PathFollower, GoesFromWhereItStartsAndPingPongs) {
  Yard y;
  const EntityId lift = y.follower(route({{0, 0}, {0, 100}}, PathFollowerComponent::Mode::PingPong), {10, 20});
  y.tick(50);  // one second at 50 per second
  EXPECT_NEAR(y.at(lift).y, 70.0f, 1e-3f);
  y.tick(100);  // up to 120, then back down half way
  EXPECT_NEAR(y.at(lift).y, 70.0f, 1e-3f);
  EXPECT_FLOAT_EQ(y.world.getComponent<PathFollowerComponent>(lift)->direction, -1.0f);
  EXPECT_FLOAT_EQ(y.at(lift).x, 10.0f);
}

TEST(PathFollower, LoopsRoundAndStopsOnceDone) {
  Yard y;
  const EntityId loop = y.follower(route({{0, 0}, {100, 0}, {100, 100}}, PathFollowerComponent::Mode::Loop, 100.0f));
  const EntityId once = y.follower(route({{0, 0}, {30, 0}}, PathFollowerComponent::Mode::Once, 100.0f));
  y.tick(177);  // 354 units: round the 341.4-unit triangle and on
  EXPECT_NEAR(y.at(loop).x, 354.0f - 341.42f, 0.01f);
  EXPECT_NEAR(y.at(loop).y, 0.0f, 1e-3f);
  EXPECT_EQ(y.at(once), glm::vec2(30.0f, 0.0f));
}

TEST(PathFollower, WaitsAtEachPointAndEasesWhenSmooth) {
  Yard y;
  PathFollowerComponent c = route({{0, 0}, {50, 0}}, PathFollowerComponent::Mode::PingPong);
  c.wait = 0.5f;
  const EntityId waiter = y.follower(c);
  y.tick(50 + 20);  // arrives after 1 s, then waits
  EXPECT_FLOAT_EQ(y.at(waiter).x, 50.0f);
  y.tick(10);  // 0.5 s in all: on its way back
  EXPECT_LT(y.at(waiter).x, 50.0f);

  c = route({{0, 0}, {100, 0}}, PathFollowerComponent::Mode::Once);
  c.smooth = true;
  const EntityId smooth = y.follower(c);
  y.tick(10);  // 10 units along: eased, behind
  EXPECT_LT(y.at(smooth).x, 10.0f);
  EXPECT_GT(y.at(smooth).x, 0.0f);
}

TEST(PathFollower, ASolidOneWithAVelocityCarriesItsRiders) {
  Yard y;
  const EntityId lift = y.follower(route({{0, 0}, {0, 100}}, PathFollowerComponent::Mode::Once));
  auto& box = y.world.addComponent<BoxColliderComponent>(lift);
  box.halfExtents = {20, 2};
  box.blocksMask = 0xFFFFFFFFu;
  y.world.addComponent<VelocityComponent>(lift).motion = kMoveMotion;
  const EntityId rider = y.world.createEntity();
  y.world.addComponent<TransformComponent>(rider).position = {0, 12.01f, 0};
  y.world.addComponent<BoxColliderComponent>(rider).halfExtents = {5, 10};
  y.world.addComponent<VelocityComponent>(rider).motion = kMoveMotion;
  y.tick(50);
  EXPECT_NEAR(y.at(lift).y, 50.0f, 1e-3f);
  EXPECT_NEAR(y.at(rider).y, 62.01f, 0.05f);  // carried up, still standing on it
}

TEST(PathFollower, AnEndAHairAwayStillTurnsItAndATwoPointLoopComesBack) {
  Yard y;
  const EntityId lift = y.follower(route({{0, 0}, {0, 96}}, PathFollowerComponent::Mode::PingPong, 48.0f));
  const EntityId loop = y.follower(route({{0, 0}, {100, 0}}, PathFollowerComponent::Mode::Loop, 100.0f));
  y.tick(150);  // 3 s: up in 2, half way down
  EXPECT_NEAR(y.at(lift).y, 48.0f, 0.05f);
  EXPECT_NEAR(y.at(loop).x, 100.0f, 0.05f);  // there and back along the line: 300 units, at the far end again
}

TEST(PathFollower, AChildGoesAlongFromItsPlaceUnderItsParent) {
  Yard y;
  y.world.registerComponent<LocalTransformComponent>();
  const EntityId child = y.follower(route({{0, 0}, {10, 0}}, PathFollowerComponent::Mode::Once, 100.0f));
  y.world.addComponent<LocalTransformComponent>(child).position = {5, 5, 0};
  y.tick(10);
  EXPECT_EQ(glm::vec2(y.world.getComponent<LocalTransformComponent>(child)->position), glm::vec2(15, 5));
}

// A point a hair short of where it stops still stops it: its wait isn't skipped.
TEST(PathFollower, APointAHairAwayStillGetsItsWait) {
  Yard y;
  PathFollowerComponent c = route({{0, 0}, {10, 0}, {20, 0}}, PathFollowerComponent::Mode::Once, 20.0f);
  c.wait = 0.2f;
  const EntityId id = y.follower(std::move(c));
  y.tick(30);  // 0.6 s: at 10 by 0.5, waiting there until 0.7
  EXPECT_NEAR(y.at(id).x, 10.0f, 1e-3f);
}

// Attached to a parent or let go on the way, it goes on from where it is.
TEST(PathFollower, AttachedOrDetachedItGoesOnFromWhereItIs) {
  Yard y;
  installTransformHierarchy(y.world);
  const EntityId parent = y.world.createEntity();
  y.world.addComponent<TransformComponent>(parent).position = {100, 0, 0};
  const EntityId id = y.follower(route({{0, 0}, {100, 0}}, PathFollowerComponent::Mode::Once, 50.0f));
  y.tick(10);  // at 10
  ASSERT_TRUE(y.world.setParent(id, parent));
  y.tick();
  EXPECT_NEAR(y.world.getComponent<LocalTransformComponent>(id)->position.x, -89.0f, 1e-3f);
  ASSERT_TRUE(y.world.setParent(id, kNoEntityId));
  const float letGo = y.at(id).x;  // (the yard runs no hierarchy system: where its transform was left)
  y.tick();
  EXPECT_NEAR(y.at(id).x, letGo + 1.0f, 1e-3f);
}
