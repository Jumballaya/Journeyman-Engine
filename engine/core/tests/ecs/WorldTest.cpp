#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "TestComponents.hpp"
#include "World.hpp"
#include "component/ComponentSpec.hpp"

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

// Destroying an entity removes it from tag lookups.
TEST(World, DestroyEntityRemovesFromTagLookup) {
  World world;
  EntityId id = world.createEntity();
  world.addTag(id, "enemy");

  EXPECT_TRUE(world.findWithTag("enemy").contains(id));

  world.destroyEntity(id);
  EXPECT_FALSE(world.findWithTag("enemy").contains(id));
}

TEST(World, RemoveTagLeavesOtherTags) {
  World world;
  EntityId id = world.createEntity();
  world.addTag(id, "b");
  world.addTag(id, "a");
  world.removeTag(id, "b");
  world.removeTag(id, "never");
  EXPECT_FALSE(world.hasTag(id, "b"));
  EXPECT_TRUE(world.findWithTag("b").empty());
  EXPECT_EQ(world.tagNames(id), std::vector<std::string>{"a"});
}

// Tools list every live entity, including ones without components.
TEST(World, EntitiesListsEveryLiveEntity) {
  World world;
  registerForTest<Position>(world);
  EntityId bare = world.createEntity();
  EntityId placed = world.createEntity();
  world.addComponent<Position>(placed);
  EntityId gone = world.createEntity();
  world.destroyEntity(gone);

  std::vector<EntityId> ids = world.entities();
  std::sort(ids.begin(), ids.end());
  EXPECT_EQ(ids, (std::vector<EntityId>{bare, placed}));
}

namespace {
struct Named : Component<Named> {
  COMPONENT_NAME("Named");
  std::string text;
};
}  // namespace

// The new component is built before rows move, so it may be copied from a
// component in the column that grows to make room for it.
TEST(World, AddComponentCopiedFromSameColumn) {
  World world;
  registerForTest<Named>(world);
  EntityId source = world.createEntity();
  world.addComponent<Named>(source, Named{.text = "a name too long for small-string storage"});
  for (int i = 0; i < 64; ++i) {
    EntityId copy = world.createEntity();
    world.addComponent<Named>(copy, *world.getComponent<Named>(source));
    EXPECT_EQ(world.getComponent<Named>(copy)->text, world.getComponent<Named>(source)->text);
  }
}

// Mutating one entity's component does not leak into another entity's
// component of the same type — their storage slots are independent.
TEST(World, ComponentDataIndependentAcrossEntities) {
  World world;
  registerForTest<Position>(world);
  EntityId a = world.createEntity();
  EntityId b = world.createEntity();
  world.addComponent<Position>(a, Position{.x = 1.0f, .y = 2.0f});
  world.addComponent<Position>(b, Position{.x = 3.0f, .y = 4.0f});

  Position* pa = world.getComponent<Position>(a);
  Position* pb = world.getComponent<Position>(b);
  ASSERT_NE(pa, nullptr);
  ASSERT_NE(pb, nullptr);

  pa->x = 99.0f;
  EXPECT_FLOAT_EQ(pb->x, 3.0f) << "mutating A's Position affected B's";
  EXPECT_FLOAT_EQ(pb->y, 4.0f);
  EXPECT_FLOAT_EQ(pa->x, 99.0f);
}

// When a component's storage is registered but the entity was never given one,
// getComponent returns nullptr rather than crashing. This is the common
// "does this entity have X?" branch pattern that systems rely on.
TEST(World, GetComponentReturnsNullWhenNeverAdded) {
  World world;
  registerForTest<Position>(world);
  EntityId id = world.createEntity();
  EXPECT_EQ(world.getComponent<Position>(id), nullptr);
}

// The createEntity(tag) convenience overload creates the entity and tags it
// in one call.
TEST(World, CreateEntityWithTagCreatesAndTags) {
  World world;
  EntityId id = world.createEntity("enemy");
  EXPECT_TRUE(world.isAlive(id));
  EXPECT_TRUE(world.hasTag(id, "enemy"));
}

namespace {
struct Ship : Component<Ship> {
  COMPONENT_NAME("Ship");
  float speed = 0.0f;
  uint32_t mask = 0;
};

void registerShip(World& world) {
  world.registerComponent<Ship>({.scriptFields = {
                                     scriptField<Ship>("speed", [](Ship& s) -> float& { return s.speed; }),
                                     scriptField<Ship>("mask", [](Ship& s) -> uint32_t& { return s.mask; }),
                                 }});
}
}  // namespace

TEST(World, ScriptFieldsReadAndWriteRawBits) {
  World world;
  registerShip(world);
  EntityId id = world.createEntity();
  world.addComponent<Ship>(id);

  auto speed = world.findScriptField("Ship", "speed");
  auto mask = world.findScriptField("Ship", "mask");
  ASSERT_TRUE(speed && mask);
  EXPECT_TRUE(world.writeScriptField(id, *speed, std::bit_cast<uint32_t>(2.5f)));
  EXPECT_TRUE(world.writeScriptField(id, *mask, 0x30));
  EXPECT_FLOAT_EQ(world.getComponent<Ship>(id)->speed, 2.5f);
  EXPECT_EQ(world.getComponent<Ship>(id)->mask, 0x30u);
  EXPECT_EQ(world.readScriptField(id, *mask), 0x30u);
}

TEST(World, ScriptFieldsFailWithoutTheComponent) {
  World world;
  registerShip(world);
  EXPECT_FALSE(world.findScriptField("Ship", "nope"));
  EXPECT_FALSE(world.findScriptField("Nope", "speed"));

  EntityId bare = world.createEntity();
  auto speed = world.findScriptField("Ship", "speed");
  EXPECT_FALSE(world.readScriptField(bare, *speed));
  EXPECT_FALSE(world.writeScriptField(bare, *speed, 1));
  EXPECT_FALSE(world.hasComponentNamed(bare, "Ship"));
  world.addComponent<Ship>(bare);
  EXPECT_TRUE(world.hasComponentNamed(bare, "Ship"));
}

// Not a test: what the World's hot paths cost per call at a large scale.
// Run with --gtest_also_run_disabled_tests --gtest_filter=*EcsCost* (optimized build).
TEST(World, DISABLED_EcsCost) {
  World world;
  registerForTest<Position>(world);
  registerForTest<Velocity>(world);
  registerForTest<Health>(world);
  std::vector<EntityId> ids;
  for (int i = 0; i < 10000; ++i) {
    const EntityId id = world.createEntity();
    world.addComponent<Position>(id);
    if (i % 2 == 0) world.addComponent<Velocity>(id);
    if (i % 3 == 0) world.addComponent<Health>(id);
    ids.push_back(id);
  }
  auto time = [](const char* name, int calls, auto&& body) {
    const auto start = std::chrono::steady_clock::now();
    body();
    const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / calls;
    std::printf("BENCH %s %.2f ns\n", name, ns);
  };
  volatile float sink = 0;
  time("ecs_getComponent_10k", 100000, [&] {
    for (int r = 0; r < 10; ++r) for (EntityId id : ids) sink = sink + world.getComponent<Position>(id)->x;
  });
  time("ecs_hasComponent_10k", 100000, [&] {
    for (int r = 0; r < 10; ++r) for (EntityId id : ids) sink = sink + (world.hasComponent<Velocity>(id) ? 1.0f : 0.0f);
  });
  time("ecs_view_iterate_per_entity_10k", 5000 * 100, [&] {
    for (int r = 0; r < 100; ++r) for (auto [id, p, v] : world.view<Position, Velocity>()) sink = sink + p->x + v->dx;
  });
  time("ecs_view_create", 1000, [&] {
    for (int r = 0; r < 1000; ++r) { auto view = world.view<Health>(); sink = sink + (view.begin() == view.end() ? 0.0f : 1.0f); }
  });
}
