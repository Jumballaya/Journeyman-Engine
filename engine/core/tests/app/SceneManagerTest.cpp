#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../assets/TempDir.hpp"
#include "ApplicationEvents.hpp"
#include "AssetManager.hpp"
#include "EventBus.hpp"
#include "SceneManager.hpp"
#include "World.hpp"

namespace {

void writeScene(const TempDir& dir, const std::string& rel,
                const nlohmann::json& j) {
  dir.writeFile(rel, j.dump());
}

nlohmann::json sceneWithNamedEntities(
    const std::vector<std::string>& names) {
  nlohmann::json entities = nlohmann::json::array();
  for (const auto& n : names) {
    entities.push_back({{"name", n}});
  }
  return {{"entities", entities}};
}

enum class EventKind { Unloading, Loaded };

}  // namespace

// loadScene fires SceneLoaded once with the AssetHandle for the loaded path.
TEST(SceneManager, LoadSceneFiresSceneLoaded) {
  TempDir dir;
  writeScene(dir, "level.scene.json", sceneWithNamedEntities({"player"}));

  World world;
  AssetManager assets(dir.path());
  EventBus bus;
  SceneManager sm(world, assets, bus);

  int loadedCalls = 0;
  AssetHandle observed;
  bus.subscribe<events::SceneLoaded>(
      EVT_SceneLoaded, [&](const events::SceneLoaded& e) {
        ++loadedCalls;
        observed = e.scene;
      });

  sm.loadScene("level.scene.json");
  bus.dispatch();

  EXPECT_EQ(loadedCalls, 1);
  EXPECT_TRUE(observed.isValid());
  EXPECT_EQ(assets.getRawAsset(observed).filePath.filename().string(),
            "level.scene.json");
}

// First loadScene on a fresh manager must NOT fire SceneUnloading — there is
// nothing to unload.
TEST(SceneManager, InitialLoadSkipsUnloadEvent) {
  TempDir dir;
  writeScene(dir, "a.scene.json", sceneWithNamedEntities({}));

  World world;
  AssetManager assets(dir.path());
  EventBus bus;
  SceneManager sm(world, assets, bus);

  int unloadingCalls = 0;
  int loadedCalls = 0;
  bus.subscribe<events::SceneUnloading>(
      EVT_SceneUnloading,
      [&](const events::SceneUnloading&) { ++unloadingCalls; });
  bus.subscribe<events::SceneLoaded>(
      EVT_SceneLoaded, [&](const events::SceneLoaded&) { ++loadedCalls; });

  sm.loadScene("a.scene.json");
  bus.dispatch();

  EXPECT_EQ(unloadingCalls, 0);
  EXPECT_EQ(loadedCalls, 1);
}

// Loading two distinct scenes back-to-back fires events in this order:
// SceneLoaded{A} → SceneUnloading{A} → SceneLoaded{B}.
TEST(SceneManager, LoadSceneTwiceFiresUnloadingAndLoadedInOrder) {
  TempDir dir;
  writeScene(dir, "a.scene.json", sceneWithNamedEntities({"a_ent"}));
  writeScene(dir, "b.scene.json", sceneWithNamedEntities({"b_ent"}));

  World world;
  AssetManager assets(dir.path());
  EventBus bus;
  SceneManager sm(world, assets, bus);

  std::vector<std::pair<EventKind, AssetHandle>> seq;
  bus.subscribe<events::SceneUnloading>(
      EVT_SceneUnloading, [&](const events::SceneUnloading& e) {
        seq.push_back({EventKind::Unloading, e.scene});
      });
  bus.subscribe<events::SceneLoaded>(
      EVT_SceneLoaded, [&](const events::SceneLoaded& e) {
        seq.push_back({EventKind::Loaded, e.scene});
      });

  AssetHandle handleA = assets.loadAsset("a.scene.json");
  AssetHandle handleB = assets.loadAsset("b.scene.json");

  sm.loadScene("a.scene.json");
  sm.loadScene("b.scene.json");
  bus.dispatch();

  ASSERT_EQ(seq.size(), 3u);
  EXPECT_EQ(seq[0].first, EventKind::Loaded);
  EXPECT_EQ(seq[0].second, handleA);
  EXPECT_EQ(seq[1].first, EventKind::Unloading);
  EXPECT_EQ(seq[1].second, handleA);
  EXPECT_EQ(seq[2].first, EventKind::Loaded);
  EXPECT_EQ(seq[2].second, handleB);
}

// Reloading the same scene path is NOT a no-op: the previous instance must be
// torn down and rebuilt. Pin this so a future shortcut doesn't slip in.
TEST(SceneManager, ReloadingSameScenePathStillUnloadsFirst) {
  TempDir dir;
  writeScene(dir, "level.scene.json", sceneWithNamedEntities({"thing"}));

  World world;
  AssetManager assets(dir.path());
  EventBus bus;
  SceneManager sm(world, assets, bus);

  int unloadingCalls = 0;
  int loadedCalls = 0;
  bus.subscribe<events::SceneUnloading>(
      EVT_SceneUnloading,
      [&](const events::SceneUnloading&) { ++unloadingCalls; });
  bus.subscribe<events::SceneLoaded>(
      EVT_SceneLoaded, [&](const events::SceneLoaded&) { ++loadedCalls; });

  sm.loadScene("level.scene.json");
  sm.loadScene("level.scene.json");
  bus.dispatch();

  EXPECT_EQ(unloadingCalls, 1);
  EXPECT_EQ(loadedCalls, 2);

  // Exactly one entity tagged "thing" is alive — the previous instance was
  // destroyed before the second load created its replacement.
  EXPECT_EQ(world.findWithTag("thing").size(), 1u);
}

// Unloading destroys every entity SceneManager owns: alive flags clear, tag
// indices empty.
TEST(SceneManager, UnloadDestroysEveryEntityFromPreviousScene) {
  TempDir dir;
  writeScene(dir, "a.scene.json",
             sceneWithNamedEntities({"alpha", "beta", "gamma"}));
  writeScene(dir, "b.scene.json", sceneWithNamedEntities({}));

  World world;
  AssetManager assets(dir.path());
  EventBus bus;
  SceneManager sm(world, assets, bus);

  sm.loadScene("a.scene.json");
  ASSERT_EQ(world.findWithTag("alpha").size(), 1u);
  ASSERT_EQ(world.findWithTag("beta").size(), 1u);
  ASSERT_EQ(world.findWithTag("gamma").size(), 1u);

  std::vector<EntityId> originals;
  for (const auto* tag : {"alpha", "beta", "gamma"}) {
    auto found = world.findWithTag(tag);
    originals.insert(originals.end(), found.begin(), found.end());
  }
  ASSERT_EQ(originals.size(), 3u);

  sm.loadScene("b.scene.json");

  for (EntityId id : originals) {
    EXPECT_FALSE(world.isAlive(id));
  }
  EXPECT_TRUE(world.findWithTag("alpha").empty());
  EXPECT_TRUE(world.findWithTag("beta").empty());
  EXPECT_TRUE(world.findWithTag("gamma").empty());
}

// Empty-then-empty scene swaps work cleanly and still fire lifecycle events.
TEST(SceneManager, EmptySceneLoadsAndUnloadsCleanly) {
  TempDir dir;
  writeScene(dir, "a.scene.json", sceneWithNamedEntities({}));
  writeScene(dir, "b.scene.json", sceneWithNamedEntities({}));

  World world;
  AssetManager assets(dir.path());
  EventBus bus;
  SceneManager sm(world, assets, bus);

  int unloadingCalls = 0;
  int loadedCalls = 0;
  bus.subscribe<events::SceneUnloading>(
      EVT_SceneUnloading,
      [&](const events::SceneUnloading&) { ++unloadingCalls; });
  bus.subscribe<events::SceneLoaded>(
      EVT_SceneLoaded, [&](const events::SceneLoaded&) { ++loadedCalls; });

  EXPECT_NO_THROW(sm.loadScene("a.scene.json"));
  EXPECT_NO_THROW(sm.loadScene("b.scene.json"));
  bus.dispatch();

  EXPECT_EQ(unloadingCalls, 1);
  EXPECT_EQ(loadedCalls, 2);
}

// Scene JSON without an "entities" key is valid; SceneLoader treats it as
// empty. Pin that the SceneManager surface preserves this.
TEST(SceneManager, LoadSceneWithNoEntitiesFieldDoesNotCrash) {
  TempDir dir;
  dir.writeFile("noEntities.scene.json",
                nlohmann::json{{"name", "scene_without_entities"}}.dump());

  World world;
  AssetManager assets(dir.path());
  EventBus bus;
  SceneManager sm(world, assets, bus);

  EXPECT_NO_THROW(sm.loadScene("noEntities.scene.json"));
  EXPECT_EQ(sm.getCurrentScenePath(), "noEntities.scene.json");
}

// Entities created via World::createEntity (NOT through SceneManager) must
// survive a scene swap — SceneManager only owns what SceneLoader produced.
TEST(SceneManager, EntitiesCreatedOutsideSceneLoadAreNotOwned) {
  TempDir dir;
  writeScene(dir, "a.scene.json", sceneWithNamedEntities({"a_ent"}));
  writeScene(dir, "b.scene.json", sceneWithNamedEntities({"b_ent"}));

  World world;
  AssetManager assets(dir.path());
  EventBus bus;
  SceneManager sm(world, assets, bus);

  sm.loadScene("a.scene.json");
  EntityId external = world.createEntity("external");
  ASSERT_TRUE(world.isAlive(external));

  sm.loadScene("b.scene.json");

  EXPECT_TRUE(world.isAlive(external));
  EXPECT_EQ(world.findWithTag("external").size(), 1u);
  EXPECT_TRUE(world.findWithTag("a_ent").empty());
}

// getCurrentScenePath reflects the most recent successful load; empty before
// any load.
TEST(SceneManager, GetCurrentScenePathReflectsLoad) {
  TempDir dir;
  writeScene(dir, "foo.scene.json", sceneWithNamedEntities({}));

  World world;
  AssetManager assets(dir.path());
  EventBus bus;
  SceneManager sm(world, assets, bus);

  EXPECT_TRUE(sm.getCurrentScenePath().empty());

  sm.loadScene("foo.scene.json");
  EXPECT_EQ(sm.getCurrentScenePath(), "foo.scene.json");
}

// isTransitioning is false on a fresh manager (no scene yet loaded).
// (D.4 will flip it true under transition.)
TEST(SceneManager, IsTransitioningFalseByDefault) {
  TempDir dir;

  World world;
  AssetManager assets(dir.path());
  EventBus bus;
  SceneManager sm(world, assets, bus);

  EXPECT_FALSE(sm.isTransitioning());
}

// A plain loadScene does not flip isTransitioning — only transitionTo (D.4)
// changes the phase.
TEST(SceneManager, IsTransitioningFalseAfterPlainLoad) {
  TempDir dir;
  writeScene(dir, "foo.scene.json", sceneWithNamedEntities({}));

  World world;
  AssetManager assets(dir.path());
  EventBus bus;
  SceneManager sm(world, assets, bus);

  sm.loadScene("foo.scene.json");
  EXPECT_FALSE(sm.isTransitioning());
}

// A failed load (missing file) propagates the AssetManager exception and
// leaves the previously loaded scene untouched.
TEST(SceneManager, LoadSceneWithMissingFileThrows) {
  TempDir dir;
  writeScene(dir, "good.scene.json", sceneWithNamedEntities({"keeper"}));

  World world;
  AssetManager assets(dir.path());
  EventBus bus;
  SceneManager sm(world, assets, bus);

  sm.loadScene("good.scene.json");
  ASSERT_EQ(world.findWithTag("keeper").size(), 1u);

  EXPECT_THROW(sm.loadScene("does-not-exist.scene.json"), std::runtime_error);

  EXPECT_EQ(sm.getCurrentScenePath(), "good.scene.json");
  EXPECT_EQ(world.findWithTag("keeper").size(), 1u);
}
