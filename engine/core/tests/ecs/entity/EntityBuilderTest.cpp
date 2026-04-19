#include <gtest/gtest.h>

#include "../TestComponents.hpp"
#include "World.hpp"

// The builder applies registered components and tags before returning the
// finished entity.
TEST(EntityBuilder, AddsComponentsAndTags) {
  World world;
  registerForTest<Position>(world);
  registerForTest<Velocity>(world);
  EntityId id = world.builder()
                    .with<Position>(Position{.x = 5.0f, .y = 6.0f})
                    .with<Velocity>(Velocity{.dx = 0.5f, .dy = -0.5f})
                    .tag("enemy")
                    .build();

  ASSERT_TRUE(world.isAlive(id));

  Position* p = world.getComponent<Position>(id);
  ASSERT_NE(p, nullptr);
  EXPECT_FLOAT_EQ(p->x, 5.0f);
  EXPECT_FLOAT_EQ(p->y, 6.0f);

  Velocity* v = world.getComponent<Velocity>(id);
  ASSERT_NE(v, nullptr);
  EXPECT_FLOAT_EQ(v->dx, 0.5f);
  EXPECT_FLOAT_EQ(v->dy, -0.5f);

  EXPECT_TRUE(world.hasTag(id, "enemy"));
}

// The with<T>(lambda) overload runs the lambda against a default-constructed
// T and then emplaces the result into the entity.
TEST(EntityBuilder, WithLambdaInitializer) {
  World world;
  registerForTest<Position>(world);
  EntityId id = world.builder()
                    .with<Position>([](Position& p) {
                      p.x = 10.0f;
                      p.y = 20.0f;
                    })
                    .build();

  Position* p = world.getComponent<Position>(id);
  ASSERT_NE(p, nullptr);
  EXPECT_FLOAT_EQ(p->x, 10.0f);
  EXPECT_FLOAT_EQ(p->y, 20.0f);
}

// A builder with no components and no tags still produces a live entity.
TEST(EntityBuilder, EmptyBuildYieldsLiveEntity) {
  World world;
  EntityId id = world.builder().build();
  EXPECT_TRUE(world.isAlive(id));
}
