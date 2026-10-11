#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "../assets/TempDir.hpp"
#include "AssetManager.hpp"
#include "EntitySpawner.hpp"
#include "EventBus.hpp"
#include "SceneManager.hpp"
#include "World.hpp"

namespace {
struct Pos : Component<Pos> {
  COMPONENT_NAME("TransformComponent");
  float x = 0, y = 0, z = 0;
  float hp = 0;
};

void registerPos(World& world) {
  world.registerComponent<Pos>({.fromJson = [](Pos& p, const nlohmann::json& j, EntityId) {
    if (j.contains("position")) {
      p.x = j["position"][0]; p.y = j["position"][1]; p.z = j["position"][2];
    }
    p.hp = j.value("hp", 0.0f);
  }});
}

struct Fixture {
  TempDir dir;
  World world;
  EventBus bus;
  std::unique_ptr<AssetManager> assets;
  std::unique_ptr<SceneManager> scenes;
  std::unique_ptr<EntitySpawner> spawner;

  Fixture() {
    dir.writeFile("bullet.prefab.json",
                  R"({"components":{"TransformComponent":{"position":[0,0,5],"hp":1}},"tags":["bullet"]})");
    dir.writeFile("empty.scene.json", R"({"entities":[]})");
    registerPos(world);
    assets = std::make_unique<AssetManager>(dir.path());
    scenes = std::make_unique<SceneManager>(world, *assets, bus);
    spawner = std::make_unique<EntitySpawner>(world, *assets, *scenes);
    scenes->loadScene("empty.scene.json");
  }
};
}  // namespace

// The id returned by spawn() is the id of the instantiated entity; position
// override keeps the prefab's z; other overrides merge.
TEST(EntitySpawner, SpawnInstantiatesOnFlushWithOverrides) {
  Fixture f;
  EntityId id = f.spawner->spawn("bullet.prefab.json", 10, 20,
                                 {{"TransformComponent", {{"hp", 3}}}});
  EXPECT_EQ(f.world.getComponent<Pos>(id), nullptr);  // not yet

  f.spawner->flush();
  Pos* p = f.world.getComponent<Pos>(id);
  ASSERT_NE(p, nullptr);
  EXPECT_FLOAT_EQ(p->x, 10);
  EXPECT_FLOAT_EQ(p->y, 20);
  EXPECT_FLOAT_EQ(p->z, 5);
  EXPECT_FLOAT_EQ(p->hp, 3);
  EXPECT_TRUE(f.world.hasTag(id, "bullet"));
}

// Spawned entities belong to the current scene and die with it.
TEST(EntitySpawner, SpawnedEntitiesUnloadWithScene) {
  Fixture f;
  EntityId id = f.spawner->spawn("bullet.prefab.json", 0, 0);
  f.spawner->flush();
  ASSERT_TRUE(f.world.isAlive(id));
  f.scenes->loadScene("empty.scene.json");
  EXPECT_FALSE(f.world.isAlive(id));
}

TEST(EntitySpawner, DeferredDestroyAppliesOnFlush) {
  Fixture f;
  EntityId id = f.spawner->spawn("bullet.prefab.json", 0, 0);
  f.spawner->flush();
  f.world.destroyDeferred(id);
  EXPECT_TRUE(f.world.isAlive(id));
  EXPECT_TRUE(f.world.isPendingDestroy(id));
  f.spawner->flush();
  EXPECT_FALSE(f.world.isAlive(id));
  EXPECT_FALSE(f.world.isPendingDestroy(id));
}

struct Doom : Component<Doom> {
  COMPONENT_NAME("Doom");
  EntityId next = kNoEntityId;
};

TEST(EntitySpawner, DestroysADestroyHookAsksForGoInTheSameFlush) {
  Fixture f;
  f.world.registerComponent<Doom>({.onDestroy = [&f](Doom& d) {
    if (d.next != kNoEntityId) f.world.destroyDeferred(d.next);
  }});
  const EntityId a = f.spawner->spawn("bullet.prefab.json", 0, 0), b = f.spawner->spawn("bullet.prefab.json", 0, 0);
  f.spawner->flush();
  f.world.addComponent<Doom>(a).next = b;
  f.world.destroyDeferred(a);
  f.spawner->flush();
  EXPECT_FALSE(f.world.isAlive(b));
}

// What a destroy hook spawns joins the scene it was destroyed in, and leaves with it.
TEST(EntitySpawner, ADestroyHooksSpawnsBelongToItsScene) {
  Fixture f;
  EntityId explosion = kNoEntityId;
  f.world.registerComponent<Doom>({.onDestroy = [&](Doom&) { explosion = f.spawner->spawn("bullet.prefab.json", 42, 0); }});
  const EntityId a = f.spawner->spawn("bullet.prefab.json", 0, 0);
  f.spawner->flush();
  f.world.addComponent<Doom>(a);
  f.world.destroyDeferred(a);
  f.spawner->flush();
  ASSERT_NE(f.world.getComponent<Pos>(explosion), nullptr);
  f.scenes->loadScene("empty.scene.json");
  f.spawner->flush();
  EXPECT_FALSE(f.world.isAlive(explosion));
}

TEST(EntitySpawner, DestroyHooksCanTellASceneUnload) {
  Fixture f;
  std::vector<bool> unloading;
  f.world.registerComponent<Doom>({.onDestroy = [&](Doom&) { unloading.push_back(f.scenes->unloading()); }});
  const EntityId a = f.spawner->spawn("bullet.prefab.json", 0, 0), b = f.spawner->spawn("bullet.prefab.json", 0, 0);
  f.spawner->flush();
  f.world.addComponent<Doom>(a);
  f.world.addComponent<Doom>(b);
  f.world.destroyDeferred(a);
  f.spawner->flush();
  f.scenes->unload();
  EXPECT_EQ(unloading, (std::vector<bool>{false, true}));
}

TEST(EntitySpawner, MissingPrefabReleasesReservedId) {
  Fixture f;
  EntityId id = f.spawner->spawn("nope.prefab.json", 0, 0);
  f.spawner->flush();
  EXPECT_FALSE(f.world.isAlive(id));
}

// A nonsense override is reported, the reserved id is released, and later
// spawns in the same flush still happen.
TEST(EntitySpawner, MalformedOverrideDoesNotAbortFlush) {
  Fixture f;
  EntityId bad = f.spawner->spawn("bullet.prefab.json", 0, 0, {{"TransformComponent", 5}});
  EntityId good = f.spawner->spawn("bullet.prefab.json", 1, 2);
  EXPECT_NO_THROW(f.spawner->flush());
  EXPECT_NE(f.world.getComponent<Pos>(good), nullptr);
  (void)bad;
}

// "tags" in the overrides tag the entity instead of merging into a component.
TEST(EntitySpawner, OverrideTagsAreAdded) {
  Fixture f;
  EntityId id = f.spawner->spawn("bullet.prefab.json", 0, 0, {{"tags", {"door", "locked"}}});
  f.spawner->flush();
  EXPECT_TRUE(f.world.hasTag(id, "bullet"));
  EXPECT_TRUE(f.world.hasTag(id, "door"));
  EXPECT_TRUE(f.world.hasTag(id, "locked"));
}

// Changes queued for an entity spawned this frame run once it exists; other
// ids aren't waiting, so nothing is queued for them.
TEST(EntitySpawner, WhenSpawnedRunsAfterInstantiation) {
  Fixture f;
  EntityId id = f.spawner->spawn("bullet.prefab.json", 0, 0);
  float seen = -1;
  EXPECT_TRUE(f.spawner->whenSpawned(id, [&]() { seen = f.world.getComponent<Pos>(id)->hp; }));
  EXPECT_FALSE(f.spawner->whenSpawned(EntityId{999, 0}, [] {}));
  EXPECT_FLOAT_EQ(seen, -1);
  f.spawner->flush();
  EXPECT_FLOAT_EQ(seen, 1);
  EXPECT_FALSE(f.spawner->whenSpawned(id, [] {}));  // spawned now
}
