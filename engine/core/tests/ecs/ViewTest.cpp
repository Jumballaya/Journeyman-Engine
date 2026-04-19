#include <gtest/gtest.h>

#include <unordered_set>

#include "TestComponents.hpp"
#include "World.hpp"

// A view over <A, B> yields every entity that has both A and B.
TEST(View, YieldsAllEntitiesWithRequiredComponents) {
  World world;
  registerForTest<Position>(world);
  registerForTest<Velocity>(world);
  std::unordered_set<EntityId> expected;
  for (int i = 0; i < 10; ++i) {
    EntityId id = world.createEntity();
    world.addComponent<Position>(id);
    world.addComponent<Velocity>(id);
    expected.insert(id);
  }

  std::unordered_set<EntityId> seen;
  for (auto [id, pos, vel] : world.view<Position, Velocity>()) {
    EXPECT_NE(pos, nullptr);
    EXPECT_NE(vel, nullptr);
    seen.insert(id);
  }

  EXPECT_EQ(seen, expected);
}

// A view over <A, B> skips entities that have only A — the intersection
// filter uses every storage, not just the primary.
TEST(View, FiltersEntitiesMissingAnyRequiredComponent) {
  World world;
  registerForTest<Position>(world);
  registerForTest<Velocity>(world);
  std::unordered_set<EntityId> bothAandB;
  std::unordered_set<EntityId> onlyA;

  for (int i = 0; i < 6; ++i) {
    EntityId id = world.createEntity();
    world.addComponent<Position>(id);
    world.addComponent<Velocity>(id);
    bothAandB.insert(id);
  }
  for (int i = 0; i < 4; ++i) {
    EntityId id = world.createEntity();
    world.addComponent<Position>(id);
    onlyA.insert(id);
  }

  std::unordered_set<EntityId> seen;
  for (auto [id, pos, vel] : world.view<Position, Velocity>()) {
    seen.insert(id);
  }
  EXPECT_EQ(seen, bothAandB);
  for (EntityId id : onlyA) {
    EXPECT_EQ(seen.count(id), 0u) << "entity with only Position leaked into <Position, Velocity> view";
  }
}

// A view whose primary storage is empty yields no elements.
TEST(View, EmptyViewHasNoElements) {
  World world;
  registerForTest<Position>(world);

  int count = 0;
  for (auto _ : world.view<Position>()) {
    (void)_;
    ++count;
  }
  EXPECT_EQ(count, 0);
}
