#include <gtest/gtest.h>

#include <stdexcept>
#include <unordered_set>
#include <vector>

#include "TestComponents.hpp"
#include "World.hpp"

//
// Entity lifecycle
//

// A freshly-created entity is alive.
TEST(World, NewEntityIsAlive) {
  World world;
  EntityId id = world.createEntity();
  EXPECT_TRUE(world.isAlive(id));
}

// Destroying an entity makes it no longer alive.
TEST(World, DestroyedEntityIsNotAlive) {
  World world;
  EntityId id = world.createEntity();
  world.destroyEntity(id);
  EXPECT_FALSE(world.isAlive(id));
}

// When a destroyed entity's index is reused for a new entity, the old
// EntityId is still reported dead because the generation was bumped. This is
// the core use-after-free guard.
TEST(World, DestroyedIdInvalidatedAfterIndexReuse) {
  World world;
  EntityId first = world.createEntity();
  world.destroyEntity(first);

  EntityId second = world.createEntity();
  EXPECT_EQ(second.index, first.index) << "index should have been reused";
  EXPECT_NE(second.generation, first.generation);
  EXPECT_FALSE(world.isAlive(first));
  EXPECT_TRUE(world.isAlive(second));
}

// Creating many entities yields distinct IDs and they can all be destroyed.
TEST(World, ManyEntitiesDistinctIds) {
  constexpr int N = 500;
  World world;
  std::unordered_set<EntityId> ids;
  std::vector<EntityId> created;
  created.reserve(N);

  for (int i = 0; i < N; ++i) {
    EntityId id = world.createEntity();
    EXPECT_TRUE(ids.insert(id).second) << "duplicate EntityId produced";
    created.push_back(id);
  }
  EXPECT_EQ(ids.size(), static_cast<size_t>(N));

  for (EntityId id : created) {
    EXPECT_TRUE(world.isAlive(id));
    world.destroyEntity(id);
    EXPECT_FALSE(world.isAlive(id));
  }
}

//
// Components
//

// Adding a component and fetching it returns the same value.
TEST(World, AddAndGetComponentRoundtrip) {
  World world;
  registerForTest<Position>(world);
  EntityId id = world.createEntity();
  world.addComponent<Position>(id, Position{.x = 1.5f, .y = -2.0f});

  Position* p = world.getComponent<Position>(id);
  ASSERT_NE(p, nullptr);
  EXPECT_FLOAT_EQ(p->x, 1.5f);
  EXPECT_FLOAT_EQ(p->y, -2.0f);
}

// After removeComponent, hasComponent reports false.
TEST(World, RemoveComponentMakesHasReturnFalse) {
  World world;
  registerForTest<Position>(world);
  EntityId id = world.createEntity();
  world.addComponent<Position>(id);
  EXPECT_TRUE(world.hasComponent<Position>(id));

  world.removeComponent<Position>(id);
  EXPECT_FALSE(world.hasComponent<Position>(id));
}

// Adding the same component twice to the same entity throws.
TEST(World, AddDuplicateComponentThrows) {
  World world;
  registerForTest<Position>(world);
  EntityId id = world.createEntity();
  world.addComponent<Position>(id);
  EXPECT_THROW(world.addComponent<Position>(id), std::runtime_error);
}

// Adding a component to a dead entity throws.
TEST(World, AddComponentToDeadEntityThrows) {
  World world;
  registerForTest<Position>(world);
  EntityId id = world.createEntity();
  world.destroyEntity(id);
  EXPECT_THROW(world.addComponent<Position>(id), std::runtime_error);
}

// getComponent on a dead entity returns nullptr (generation-aware path).
TEST(World, GetComponentOnDeadEntityReturnsNull) {
  World world;
  registerForTest<Position>(world);
  EntityId id = world.createEntity();
  world.addComponent<Position>(id);
  world.destroyEntity(id);

  EXPECT_EQ(world.getComponent<Position>(id), nullptr);
}

// removeComponent on a dead entity is a silent no-op rather than an error.
TEST(World, RemoveComponentOnDeadEntityIsNoOp) {
  World world;
  registerForTest<Position>(world);
  EntityId id = world.createEntity();
  world.addComponent<Position>(id);
  world.destroyEntity(id);

  EXPECT_NO_THROW(world.removeComponent<Position>(id));
}

//
// Clone
//

// cloneEntity copies registered components, and the clone owns its own copy —
// mutating the clone does not affect the source.
TEST(World, CloneEntityCopiesComponents) {
  World world;
  registerForTest<Position>(world);
  registerForTest<Velocity>(world);

  EntityId src = world.createEntity();
  world.addComponent<Position>(src, Position{.x = 1.0f, .y = 2.0f});
  world.addComponent<Velocity>(src, Velocity{.dx = 3.0f, .dy = 4.0f});

  EntityId dst = world.cloneEntity(src);
  ASSERT_TRUE(world.isAlive(dst));
  ASSERT_NE(dst, src);

  Position* srcP = world.getComponent<Position>(src);
  Position* dstP = world.getComponent<Position>(dst);
  ASSERT_NE(srcP, nullptr);
  ASSERT_NE(dstP, nullptr);
  EXPECT_FLOAT_EQ(dstP->x, 1.0f);
  EXPECT_FLOAT_EQ(dstP->y, 2.0f);

  dstP->x = 99.0f;
  EXPECT_FLOAT_EQ(srcP->x, 1.0f) << "clone mutation leaked into source";

  Velocity* dstV = world.getComponent<Velocity>(dst);
  ASSERT_NE(dstV, nullptr);
  EXPECT_FLOAT_EQ(dstV->dx, 3.0f);
  EXPECT_FLOAT_EQ(dstV->dy, 4.0f);
}

// cloneEntity copies all tags from the source entity.
TEST(World, CloneEntityCopiesTags) {
  World world;

  EntityId src = world.createEntity();
  world.addTag(src, "enemy");
  world.addTag(src, "flying");

  EntityId dst = world.cloneEntity(src);
  ASSERT_TRUE(world.isAlive(dst));

  EXPECT_TRUE(world.hasTag(dst, "enemy"));
  EXPECT_TRUE(world.hasTag(dst, "flying"));
}

//
// Tags
//

// Adding a tag and then querying hasTag reports true.
TEST(World, AddAndHasTagRoundtrip) {
  World world;
  EntityId id = world.createEntity();
  world.addTag(id, "enemy");
  EXPECT_TRUE(world.hasTag(id, "enemy"));
  EXPECT_FALSE(world.hasTag(id, "ally"));
}

// findWithTag returns every entity that holds the given tag.
TEST(World, FindWithTagReturnsMembers) {
  World world;
  EntityId a = world.createEntity();
  EntityId b = world.createEntity();
  EntityId c = world.createEntity();

  world.addTag(a, "enemy");
  world.addTag(b, "enemy");
  world.addTag(c, "ally");

  auto enemies = world.findWithTag("enemy");
  EXPECT_EQ(enemies.size(), 2u);
  EXPECT_TRUE(enemies.contains(a));
  EXPECT_TRUE(enemies.contains(b));
  EXPECT_FALSE(enemies.contains(c));
}

// findWithTags returns the intersection of tag memberships.
TEST(World, FindWithTagsReturnsIntersection) {
  World world;
  EntityId flyingEnemy = world.createEntity();
  EntityId groundEnemy = world.createEntity();
  EntityId flyingAlly = world.createEntity();

  world.addTag(flyingEnemy, "enemy");
  world.addTag(flyingEnemy, "flying");
  world.addTag(groundEnemy, "enemy");
  world.addTag(flyingAlly, "flying");
  world.addTag(flyingAlly, "ally");

  auto both = world.findWithTags({"enemy", "flying"});
  EXPECT_EQ(both.size(), 1u);
  EXPECT_TRUE(both.contains(flyingEnemy));
  EXPECT_FALSE(both.contains(groundEnemy));
  EXPECT_FALSE(both.contains(flyingAlly));
}

// Destroying an entity removes it from tag lookups.
TEST(World, DestroyEntityRemovesFromTagLookup) {
  World world;
  EntityId id = world.createEntity();
  world.addTag(id, "enemy");

  EXPECT_TRUE(world.findWithTag("enemy").contains(id));

  world.destroyEntity(id);
  EXPECT_FALSE(world.findWithTag("enemy").contains(id));
}
