#include <gtest/gtest.h>

#include "SoftFollow.hpp"
#include "TransformHierarchy.hpp"

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

// Under a parent (rotated or not) it still goes to its target: through its local place, which the hierarchy keeps.
TEST(SoftFollow, AFollowerUnderAParentGetsThere) {
  for (float turn : {0.0f, 1.0f}) {
    Yard y(trailing({0, 0}, 0.0f));
    installTransformHierarchy(y.world);
    const EntityId box = y.world.createEntity();
    auto& t = y.world.addComponent<TransformComponent>(box);
    t.position = {-200, 120, 0};
    t.rotationRad = turn;
    y.world.setParent(y.pet, box);
    y.system.update(y.world, 0.02f);
    placeChild(y.of(box), *y.world.getComponent<LocalTransformComponent>(y.pet), y.of(y.pet));  // as the hierarchy will
    EXPECT_NEAR(y.at(y.pet).x, 100.0f, 1e-3f) << turn;
    EXPECT_NEAR(y.at(y.pet).y, 50.0f, 1e-3f) << turn;
  }
}

// A target under a parent that moved this frame is followed to where it is now.
TEST(SoftFollow, AChildTargetIsFollowedToWhereItIsNow) {
  Yard y(trailing({0, 0}, 0.0f));
  installTransformHierarchy(y.world);
  const EntityId cart = y.world.createEntity();
  y.world.addComponent<TransformComponent>(cart).position = {0, 0, 0};
  y.world.setParent(y.hero, cart);  // the hero rides at (100, 50) from the cart
  y.of(cart).position.x = 10;       // the cart moved; the hierarchy hasn't run yet
  y.system.update(y.world, 0.02f);
  EXPECT_EQ(y.at(y.pet), glm::vec2(110, 50));
}

// A follower under a follower: the parent goes first, whatever order the world keeps them in.
TEST(SoftFollow, AFollowerUnderAFollowerGetsThereToo) {
  World world;
  world.registerComponent<TransformComponent>();
  world.registerComponent<SoftFollowComponent>();
  installTransformHierarchy(world);
  SoftFollowSystem system;
  // Children with a follow are kept first: their kind of row is made before the parent's.
  const EntityId early = world.createEntity();
  world.addComponent<TransformComponent>(early);
  world.addComponent<LocalTransformComponent>(early);
  world.addComponent<SoftFollowComponent>(early);
  const EntityId hero = world.createEntity("hero");
  world.addComponent<TransformComponent>(hero).position = {100, 0, 0};
  const auto follower = [&](glm::vec2 at) {
    const EntityId id = world.createEntity();
    world.addComponent<TransformComponent>(id).position = {at, 0.0f};
    world.addComponent<SoftFollowComponent>(id, trailing({0, 0}, 0.0f)).target.name = "hero";
    return id;
  };
  const EntityId pet = follower({0, 0}), kit = follower({20, 0});
  world.setParent(kit, pet);
  system.update(world, 0.02f);
  TransformComponent& placed = *world.getComponent<TransformComponent>(kit);
  placeChild(*world.getComponent<TransformComponent>(pet), *world.getComponent<LocalTransformComponent>(kit), placed);
  EXPECT_EQ(glm::vec2(placed.position), glm::vec2(100, 0));
}
