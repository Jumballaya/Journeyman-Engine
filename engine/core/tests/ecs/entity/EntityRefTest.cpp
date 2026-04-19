#include <gtest/gtest.h>

#include "../TestComponents.hpp"
#include "World.hpp"

// world[id] returns an EntityRef whose add/get/has/remove operations delegate
// correctly to the underlying World.
TEST(EntityRef, AddGetHasRemoveRoundtrip) {
  World world;
  registerForTest<Position>(world);
  EntityId id = world.createEntity();
  auto ref = world[id];

  EXPECT_FALSE(ref.has<Position>());

  Position& added = ref.add<Position>(Position{.x = 7.0f, .y = 8.0f});
  EXPECT_FLOAT_EQ(added.x, 7.0f);
  EXPECT_TRUE(ref.has<Position>());

  Position* got = ref.get<Position>();
  ASSERT_NE(got, nullptr);
  EXPECT_FLOAT_EQ(got->x, 7.0f);
  EXPECT_FLOAT_EQ(got->y, 8.0f);

  ref.remove<Position>();
  EXPECT_FALSE(ref.has<Position>());
}

// ref.alive() tracks the world's liveness state for the referenced entity.
TEST(EntityRef, AliveReflectsWorldState) {
  World world;
  EntityId id = world.createEntity();
  auto ref = world[id];

  EXPECT_TRUE(ref.alive());
  world.destroyEntity(id);
  EXPECT_FALSE(ref.alive());
}
