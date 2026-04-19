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

// Iteration yields non-const T* pointers — mutating a component through the
// pointer persists in the underlying storage. Systems depend on this.
TEST(View, IterationYieldsMutablePointers) {
  World world;
  registerForTest<Position>(world);
  EntityId id = world.createEntity();
  world.addComponent<Position>(id, Position{.x = 1.0f, .y = 2.0f});

  for (auto [eid, pos] : world.view<Position>()) {
    (void)eid;
    pos->x = 42.0f;
  }

  Position* p = world.getComponent<Position>(id);
  ASSERT_NE(p, nullptr);
  EXPECT_FLOAT_EQ(p->x, 42.0f);
}

// destroyEntity does NOT clear the entity's component storage, so the dead
// entity's EntityId is still yielded by view iteration with its stale data.
// Pins the current behavior so the archetype refactor has an explicit
// decision point.
TEST(View, DestroyedEntityComponentsLingerInIteration) {
  World world;
  registerForTest<Position>(world);
  EntityId a = world.createEntity();
  EntityId b = world.createEntity();
  world.addComponent<Position>(a, Position{.x = 1.0f});
  world.addComponent<Position>(b, Position{.x = 2.0f});

  world.destroyEntity(a);
  ASSERT_FALSE(world.isAlive(a));

  int count = 0;
  bool sawDead = false;
  for (auto [id, pos] : world.view<Position>()) {
    (void)pos;
    ++count;
    if (id == a) sawDead = true;
  }

  EXPECT_EQ(count, 2);
  EXPECT_TRUE(sawDead);
}
