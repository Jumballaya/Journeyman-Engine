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
  world.registerComponent<Pos, char>(
      [](World& w, EntityId id, const nlohmann::json& j) {
        Pos p;
        if (j.contains("position")) {
          p.x = j["position"][0]; p.y = j["position"][1]; p.z = j["position"][2];
        }
        p.hp = j.value("hp", 0.0f);
        w.addComponent<Pos>(id, p);
      },
      nullptr, nullptr, nullptr);
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

TEST(EntitySpawner, MissingPrefabReleasesReservedId) {
  Fixture f;
  EntityId id = f.spawner->spawn("nope.prefab.json", 0, 0);
  f.spawner->flush();
  EXPECT_FALSE(f.world.isAlive(id));
}
