#include <gtest/gtest.h>

#include "SoftFollow.hpp"

namespace {

struct Yard {
  World world;
  SoftFollowSystem system;
  EntityId hero, pet;
  Yard(SoftFollowComponent follow) {
    world.registerComponent<TransformComponent>();
    world.registerComponent<SoftFollowComponent>();
    hero = world.createEntity("hero");
    world.addComponent<TransformComponent>(hero).position = {100, 50, 0};
    pet = world.createEntity();
    world.addComponent<TransformComponent>(pet);
    follow.target.name = "hero";
    world.addComponent<SoftFollowComponent>(pet, std::move(follow));
  }
  TransformComponent& of(EntityId id) { return *world.getComponent<TransformComponent>(id); }
  glm::vec2 at(EntityId id) { return glm::vec2(of(id).position); }
};

SoftFollowComponent trailing(glm::vec2 offset, float smoothing, bool mirror = false) {
  SoftFollowComponent c;
  c.offset = offset;
  c.smoothing = smoothing;
  c.mirror = mirror;
  return c;
}

}  // namespace

TEST(SoftFollow, StartsAtItsPlaceThenEasesAfter) {
  Yard y(trailing({-20, 10}, 8.0f));
  y.system.update(y.world, 0.02f);
  EXPECT_EQ(y.at(y.pet), glm::vec2(80, 60));  // the first frame: there at once
  y.of(y.hero).position.x = 200;
  y.system.update(y.world, 0.02f);
  EXPECT_GT(y.at(y.pet).x, 80.0f);
  EXPECT_LT(y.at(y.pet).x, 180.0f);  // lagging behind
  for (int i = 0; i < 200; ++i) y.system.update(y.world, 0.02f);
  EXPECT_NEAR(y.at(y.pet).x, 180.0f, 1e-3f);
}

TEST(SoftFollow, AMirroredOffsetStaysBehindAndNoTargetStaysPut) {
  Yard y(trailing({-20, 0}, 0.0f, true));
  y.of(y.hero).scale.x = -1.0f;  // facing left
  y.system.update(y.world, 0.02f);
  EXPECT_EQ(y.at(y.pet), glm::vec2(120, 50));
  y.world.destroyEntity(y.hero);
  y.system.update(y.world, 0.02f);
  EXPECT_EQ(y.at(y.pet), glm::vec2(120, 50));
}
