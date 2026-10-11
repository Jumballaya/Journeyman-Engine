#include <gtest/gtest.h>

#include "../../core/ecs/World.hpp"
#include "../../physics2d/TransformComponent.hpp"
#include "../../physics2d/VelocityComponent.hpp"
#include "../Cameras.hpp"

namespace {

const glm::vec2 kGame{40.0f, 40.0f};  // the game's size: a view 40 world units across at zoom 1

struct Fixture {
  World world;
  Cameras cameras;
  EntityId player;

  Fixture() {
    world.registerComponent<CameraComponent>();
    world.registerComponent<TransformComponent>();
    world.registerComponent<VelocityComponent>();
    player = world.createEntity("player");
    world.addComponent<TransformComponent>(player);
    world.addComponent<VelocityComponent>(player);
  }

  EntityId camera(CameraComponent c, glm::vec2 at = glm::vec2(0.0f)) {
    const EntityId id = world.createEntity();
    world.addComponent<TransformComponent>(id).position = glm::vec3(at, 0.0f);
    world.addComponent<CameraComponent>(id, std::move(c));
    return id;
  }
  CameraComponent following(float smoothing = 0.0f) {
    CameraComponent c;
    c.target.name = "player";
    c.smoothing = smoothing;
    return c;
  }
  void moveTo(float x, float y = 0.0f, float vx = 0.0f) {
    world.getComponent<TransformComponent>(player)->position = {x, y, 0.0f};
    world.getComponent<VelocityComponent>(player)->velocity.x = vx;
  }
  float x() const { return cameras.view().center.x; }
  void tick(float dt = 1.0f) { cameras.update(world, dt, kGame); }
};

}  // namespace

TEST(Cameras, WithoutCameraEntitiesScriptsPlaceTheView) {
  Fixture f;
  f.cameras.setPosition(f.world, {12.0f, 5.0f});
  f.cameras.setZoom(f.world, 2.0f);
  f.tick();
  EXPECT_EQ(f.cameras.view().center, glm::vec2(12.0f, 5.0f));
  EXPECT_FLOAT_EQ(f.cameras.view().zoom, 2.0f);
}

TEST(Cameras, StartsOnItsTargetThenKeepsItInTheDeadZone) {
  Fixture f;
  CameraComponent c = f.following();
  c.deadZone = {20.0f, 0.0f};
  f.camera(c, {500.0f, 0.0f});
  f.tick();
  EXPECT_FLOAT_EQ(f.x(), 0.0f);  // on the target at once, wherever it was placed
  f.moveTo(5.0f);
  f.tick();
  EXPECT_FLOAT_EQ(f.x(), 0.0f);  // inside the dead zone: stays
  f.moveTo(30.0f);
  f.tick();
  EXPECT_FLOAT_EQ(f.x(), 20.0f);  // pulled along, the target at its edge
}

TEST(Cameras, LooksAheadOfARunnerAndStaysInsideBounds) {
  Fixture f;
  CameraComponent c = f.following();
  c.deadZone = {20.0f, 0.0f};
  c.lookAhead = 8.0f;
  c.bounds = CameraComponent::Rect{{0.0f, -100.0f}, {100.0f, 100.0f}};
  f.camera(c);
  f.tick();
  f.moveTo(30.0f, 0.0f, 100.0f);
  f.tick();
  EXPECT_FLOAT_EQ(f.x(), 28.0f);
  f.moveTo(200.0f, 0.0f, 0.0f);
  f.tick();
  EXPECT_FLOAT_EQ(f.x(), 80.0f);  // the view's right edge on the bound
}

TEST(Cameras, ABoundsNarrowerThanTheViewCentersIt) {
  Fixture f;
  CameraComponent c = f.following();
  c.bounds = CameraComponent::Rect{{0.0f, 0.0f}, {30.0f, 10.0f}};
  f.camera(c);
  f.moveTo(25.0f, 3.0f);
  f.tick();
  EXPECT_EQ(f.cameras.view().center, glm::vec2(15.0f, 5.0f));
}

TEST(Cameras, ARegionHoldingTheTargetReplacesTheBounds) {
  Fixture f;
  CameraComponent c = f.following();
  c.bounds = CameraComponent::Rect{{0.0f, -100.0f}, {1000.0f, 100.0f}};
  c.regions = {{{200.0f, -100.0f}, {300.0f, 100.0f}}};
  f.camera(c);
  f.moveTo(205.0f);
  f.tick();
  EXPECT_FLOAT_EQ(f.x(), 220.0f);  // the room's left wall
  f.moveTo(150.0f);
  f.tick();
  EXPECT_FLOAT_EQ(f.x(), 150.0f);  // out of it: the level's bounds again
}

TEST(Cameras, PansOverWhenTheTargetChangesRegion) {
  Fixture f;
  CameraComponent c = f.following(8.0f);
  c.regions = {{{200.0f, -100.0f}, {300.0f, 100.0f}}};
  f.camera(c);
  f.moveTo(150.0f);
  f.tick(0.0f);
  EXPECT_FLOAT_EQ(f.x(), 150.0f);
  f.moveTo(205.0f);
  f.tick(0.1f);
  EXPECT_GT(f.x(), 150.0f);
  EXPECT_LT(f.x(), 205.0f);  // not a cut: on its way into the room
  for (int i = 0; i < 60; ++i) f.tick(0.1f);
  EXPECT_FLOAT_EQ(f.x(), 220.0f);  // there: inside the room again
}

TEST(Cameras, ScriptsPlaceTheCameraBeforeItsFirstFrame) {
  Fixture f;
  CameraComponent c;
  c.zoom = 1.0f;
  const EntityId camera = f.camera(c);
  f.cameras.setZoom(f.world, 2.0f);
  f.cameras.setPosition(f.world, {30.0f, 40.0f});
  f.tick();
  EXPECT_EQ(f.cameras.view().center, glm::vec2(30.0f, 40.0f));
  EXPECT_FLOAT_EQ(f.cameras.view().zoom, 2.0f);
  EXPECT_EQ(f.cameras.shown(), camera);
}

TEST(Cameras, EasingDoesNotDependOnHowFramesFall) {
  Fixture a, b;
  a.camera(a.following(8.0f));
  b.camera(b.following(8.0f));
  a.tick(0.0f);
  b.tick(0.0f);
  a.moveTo(100.0f);
  b.moveTo(100.0f);
  a.tick(0.2f);
  b.tick(0.1f);
  b.tick(0.1f);
  EXPECT_GT(a.x(), 0.0f);
  EXPECT_LT(a.x(), 100.0f);
  EXPECT_NEAR(a.x(), b.x(), 1e-3f);
}

TEST(Cameras, ShowsTheHighestPriorityAndBlendsWhenItChanges) {
  Fixture f;
  CameraComponent low, high;
  low.blend = 2.0f;
  high.priority = 1.0f;
  f.camera(low, {0.0f, 0.0f});
  const EntityId boss = f.camera(high, {100.0f, 0.0f});
  f.tick();
  EXPECT_FLOAT_EQ(f.x(), 100.0f);  // the first one shown: no blend from nothing
  f.world.getComponent<CameraComponent>(boss)->priority = -1.0f;
  f.tick(1.0f);  // over to the low camera, half of its 2 s
  EXPECT_FLOAT_EQ(f.x(), 50.0f);
  f.tick(1.0f);
  EXPECT_FLOAT_EQ(f.x(), 0.0f);
}

TEST(Cameras, SetPositionJumpsTheShownCameraAndFollowSwitchesTargets) {
  Fixture f;
  CameraComponent c = f.following(8.0f);
  c.lookAhead = 30.0f;
  const EntityId camera = f.camera(c);
  f.moveTo(0.0f, 0.0f, 50.0f);
  f.tick(0.5f);
  f.cameras.setPosition(f.world, {1600.0f, 40.0f});
  EXPECT_EQ(f.cameras.view().center, glm::vec2(1600.0f, 40.0f));
  EXPECT_FLOAT_EQ(f.world.getComponent<TransformComponent>(camera)->position.x, 1600.0f);
  EXPECT_FLOAT_EQ(f.world.getComponent<CameraComponent>(camera)->ahead, 0.0f);  // no look-ahead carried over

  const EntityId other = f.world.createEntity();
  f.world.addComponent<TransformComponent>(other).position = {-50.0f, 0.0f, 0.0f};
  Cameras::follow(f.world, camera, other);
  f.world.getComponent<CameraComponent>(camera)->smoothing = 0.0f;
  f.tick();
  EXPECT_FLOAT_EQ(f.x(), -50.0f);
  f.world.destroyEntity(other);
  f.tick();
  EXPECT_FLOAT_EQ(f.x(), -50.0f);  // its target gone: stays, not back to "player"
}

TEST(Cameras, FindsANamedTargetAgainAfterItRespawns) {
  Fixture f;
  f.camera(f.following());
  f.tick();
  f.world.destroyEntity(f.player);
  f.player = f.world.createEntity("player");
  f.world.addComponent<TransformComponent>(f.player).position = {70.0f, 0.0f, 0.0f};
  f.tick();
  EXPECT_FLOAT_EQ(f.x(), 70.0f);
}
