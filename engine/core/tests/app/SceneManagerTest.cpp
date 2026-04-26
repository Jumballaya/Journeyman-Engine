#include <gtest/gtest.h>

#include <cstdint>
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../../scripting/ScriptComponent.hpp"
#include "../../scripting/ScriptManager.hpp"
#include "../assets/TempDir.hpp"
#include "ApplicationEvents.hpp"
#include "AssetManager.hpp"
#include "EventBus.hpp"
#include "RawAsset.hpp"
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

// ---------------------------------------------------------------------------
// D.2 integration: ScriptComponent destruction cascade.
//
// These tests verify that destroying an entity with a ScriptComponent (via
// scene unload) releases the underlying ScriptInstance from ScriptManager.
// Without the onDestroy hook wired through ComponentInfo, the wasm instance
// would leak across scene swaps.
// ---------------------------------------------------------------------------

namespace {

// A minimal valid wasm module that exports `onUpdate(f32) -> void` with an
// empty body. ScriptInstance's constructor requires onUpdate to be present
// (it throws otherwise), so the smallest-possible test fixture must include
// it. Hand-encoded so the test has no dependency on a wasm toolchain.
//
// Sections:
//   magic + version       0x00 0x61 0x73 0x6d 0x01 0x00 0x00 0x00
//   type:    (f32)->()    0x01 0x05 0x01 0x60 0x01 0x7d 0x00
//   func 0 of type 0      0x03 0x02 0x01 0x00
//   export "onUpdate"     0x07 0x0c 0x01 0x08 'o' 'n' 'U' 'p' 'd' 'a' 't' 'e' 0x00 0x00
//   code: empty body      0x0a 0x04 0x01 0x02 0x00 0x0b
constexpr uint8_t kMinimalUpdateWasm[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x05, 0x01, 0x60, 0x01, 0x7d, 0x00,
    0x03, 0x02, 0x01, 0x00,
    0x07, 0x0c, 0x01, 0x08, 0x6f, 0x6e, 0x55, 0x70, 0x64, 0x61, 0x74, 0x65,
    0x00, 0x00,
    0x0a, 0x04, 0x01, 0x02, 0x00, 0x0b,
};

// Static context for the test's ScriptComponent onDestroy hook. Mirrors the
// pattern Engine.cpp uses — ComponentInfo's onDestroy slot is a raw function
// pointer, so we route through a file-static for the manager reference.
ScriptManager* g_testScriptManager = nullptr;

void scriptComponentOnDestroyForTest(void* ptr) {
  auto* comp = static_cast<ScriptComponent*>(ptr);
  if (g_testScriptManager && comp->instance.isValid()) {
    g_testScriptManager->destroyInstance(comp->instance);
  }
}

// Stage a minimal `.script.json` + `.wasm` pair under `dir` and register a
// converter on `assets` that decodes `.script.json` into the given
// ScriptManager. Returns the manifest-relative path of the script asset.
std::string stageScriptAsset(const TempDir& dir, AssetManager& assets,
                             ScriptManager& sm,
                             const std::string& wasmRel,
                             const std::string& scriptJsonRel) {
  std::vector<uint8_t> wasm(std::begin(kMinimalUpdateWasm),
                            std::end(kMinimalUpdateWasm));
  dir.writeFile(wasmRel, wasm);

  nlohmann::json scriptManifest{
      {"name", "test"},
      {"binary", wasmRel},
  };
  dir.writeFile(scriptJsonRel, scriptManifest.dump());

  assets.addAssetConverter({".script.json"},
      [&assets, &sm](const RawAsset& asset, const AssetHandle& manifestHandle) {
        nlohmann::json manifest = nlohmann::json::parse(
            std::string(asset.data.begin(), asset.data.end()));
        std::string binPath = manifest["binary"].get<std::string>();
        AssetHandle wasmHandle = assets.loadAsset(binPath);
        const RawAsset& wasmAsset = assets.getRawAsset(wasmHandle);
        sm.loadScript(manifestHandle, wasmAsset.data, {});
      });

  return scriptJsonRel;
}

// Register ScriptComponent on `world` with the destroy hook. The deserializer
// reads `script` as a path, loads the asset (which triggers the converter
// registered via stageScriptAsset), creates an instance, and attaches the
// component.
void registerScriptComponentForTest(World& world, AssetManager& assets,
                                    ScriptManager& sm) {
  world.registerComponent<ScriptComponent, PODScriptComponent>(
      [&assets, &sm](World& w, EntityId id, const nlohmann::json& json) {
        std::string scriptPath = json["script"].get<std::string>();
        AssetHandle scriptAsset = assets.loadAsset(scriptPath);
        ScriptInstanceHandle inst = sm.createInstance(scriptAsset, id);
        if (!inst.isValid()) return;
        w.addComponent<ScriptComponent>(id, inst);
      },
      [](const World&, EntityId, nlohmann::json&) { return false; },
      [](World&, EntityId, std::span<const std::byte>) { return false; },
      [](const World&, EntityId, std::span<std::byte>, size_t&) { return false; },
      &scriptComponentOnDestroyForTest);
}

}  // namespace

// Unloading a scene that holds a ScriptComponent must release the
// ScriptInstance from ScriptManager — getInstance(handle) returns null after
// the unload.
TEST(SceneManager, UnloadingSceneWithScriptedEntityReleasesWasmInstance) {
  TempDir dir;

  World world;
  AssetManager assets(dir.path());
  EventBus bus;
  ScriptManager scriptManager;
  g_testScriptManager = &scriptManager;

  stageScriptAsset(dir, assets, scriptManager, "test.wasm", "test.script.json");
  registerScriptComponentForTest(world, assets, scriptManager);

  nlohmann::json scriptedEntity{
      {"name", "scripted"},
      {"components", {{"ScriptComponent", {{"script", "test.script.json"}}}}},
  };
  dir.writeFile("scripted.scene.json",
                nlohmann::json{{"entities", {scriptedEntity}}}.dump());
  writeScene(dir, "empty.scene.json", sceneWithNamedEntities({}));

  SceneManager sceneManager(world, assets, bus);
  sceneManager.loadScene("scripted.scene.json");

  auto found = world.findWithTag("scripted");
  ASSERT_EQ(found.size(), 1u);
  EntityId scripted = *found.begin();
  ScriptComponent* comp = world.getComponent<ScriptComponent>(scripted);
  ASSERT_NE(comp, nullptr);
  ScriptInstanceHandle handle = comp->instance;
  ASSERT_TRUE(handle.isValid());
  ASSERT_NE(scriptManager.getInstance(handle), nullptr);
  EXPECT_EQ(scriptManager.instanceCount(), 1u);

  sceneManager.loadScene("empty.scene.json");

  EXPECT_EQ(scriptManager.getInstance(handle), nullptr);
  EXPECT_EQ(scriptManager.instanceCount(), 0u);

  g_testScriptManager = nullptr;
}

// Bouncing between two scenes — one with N scripted entities, the other
// empty — must not accumulate ScriptInstance entries beyond N at any point.
//
// Pins D.2.5's wasm runtime fix end-to-end: each createInstance parses a
// fresh module and `~ScriptInstance` frees the runtime, so re-loading the
// same scripted scene works repeatedly with no growth in instanceCount.
TEST(SceneManager, RepeatedSceneSwapsDoNotLeakWasmInstances) {
  TempDir dir;

  World world;
  AssetManager assets(dir.path());
  EventBus bus;
  ScriptManager scriptManager;
  g_testScriptManager = &scriptManager;

  stageScriptAsset(dir, assets, scriptManager, "test.wasm", "test.script.json");
  registerScriptComponentForTest(world, assets, scriptManager);

  constexpr size_t kEntitiesPerScene = 3;
  nlohmann::json entities = nlohmann::json::array();
  for (size_t i = 0; i < kEntitiesPerScene; ++i) {
    entities.push_back({
        {"name", "scripted_" + std::to_string(i)},
        {"components", {{"ScriptComponent", {{"script", "test.script.json"}}}}},
    });
  }
  dir.writeFile("scripted.scene.json",
                nlohmann::json{{"entities", entities}}.dump());
  writeScene(dir, "empty.scene.json", sceneWithNamedEntities({}));

  SceneManager sceneManager(world, assets, bus);

  for (int iter = 0; iter < 10; ++iter) {
    sceneManager.loadScene("scripted.scene.json");
    EXPECT_EQ(scriptManager.instanceCount(), kEntitiesPerScene)
        << "iter=" << iter << " (after loading scripted scene)";

    sceneManager.loadScene("empty.scene.json");
    EXPECT_EQ(scriptManager.instanceCount(), 0u)
        << "iter=" << iter << " (after loading empty scene)";
  }

  g_testScriptManager = nullptr;
}
