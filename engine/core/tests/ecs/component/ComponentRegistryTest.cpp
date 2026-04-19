#include <gtest/gtest.h>

#include "../TestComponents.hpp"
#include "World.hpp"

// The ComponentId for a given type is stable across independent World
// instances — it depends only on the component's typeName, not on
// registration order. Directly pins the hash-based ID guarantee.
TEST(ComponentRegistry, ComponentIdDeterministicAcrossInstances) {
  World a;
  World b;
  registerForTest<Position>(a);
  registerForTest<Velocity>(b);
  registerForTest<Position>(b);

  EXPECT_EQ(Position::typeId(), Position::typeId());
  // The registry in A and B should both resolve "Position" to the same id.
  auto idInA = a.getComponentRegistry().getComponentIdByName("Position");
  auto idInB = b.getComponentRegistry().getComponentIdByName("Position");
  ASSERT_TRUE(idInA.has_value());
  ASSERT_TRUE(idInB.has_value());
  EXPECT_EQ(*idInA, *idInB);
  EXPECT_EQ(*idInA, Position::typeId());
}

// The registry maps the component's name back to the same id that the type
// reports via typeId().
TEST(ComponentRegistry, LookupByNameMatchesTypeId) {
  World world;
  registerForTest<Health>(world);

  auto id = world.getComponentRegistry().getComponentIdByName("Health");
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(*id, Health::typeId());

  EXPECT_FALSE(world.getComponentRegistry().getComponentIdByName("nope").has_value());
}
