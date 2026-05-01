#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../../assets/Archive.hpp"
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

// ---------------------------------------------------------------------------
// D.4: shader-composited scene transitions.
//
// SceneManager owns the logical state machine — entity ownership swaps,
// transition state for renderer polling, lifecycle events, and the
// thread-safe request queue used by worker-thread callers (script host
// functions). The visual side (frame snapshot + Crossfade post-effect) is
// verified end-to-end in D.6's demo smoke; these unit tests cover state.
// ---------------------------------------------------------------------------

namespace {

// Two-scene test fixture used by most D.4 tests. Writes A and B scenes (each
// with a single uniquely-tagged entity) into a TempDir and constructs a
// SceneManager pointing at it.
struct TransitionFixture {
  TempDir dir;
  World world;
  AssetManager assets;
  EventBus bus;
  SceneManager sm;

  TransitionFixture()
      : assets(dir.path()), sm(world, assets, bus) {
    writeScene(dir, "a.scene.json", sceneWithNamedEntities({"a_ent"}));
    writeScene(dir, "b.scene.json", sceneWithNamedEntities({"b_ent"}));
    writeScene(dir, "c.scene.json", sceneWithNamedEntities({"c_ent"}));
  }
};

}  // namespace

// transitionTo arms the state machine: isTransitioning is true after the call,
// remains true while elapsed < duration, flips false once the duration is
// reached.
TEST(SceneManager, IsTransitioningTrueDuringTransition) {
  TransitionFixture fx;
  fx.sm.loadScene("a.scene.json");
  EXPECT_FALSE(fx.sm.isTransitioning());

  fx.sm.transitionTo("b.scene.json", TransitionConfig{0.5f});
  EXPECT_TRUE(fx.sm.isTransitioning());

  fx.sm.tick(0.5f + 0.001f);
  EXPECT_FALSE(fx.sm.isTransitioning());
}

// Successive ticks add up — only the tick that crosses the duration boundary
// finishes the transition.
TEST(SceneManager, TransitionAdvancesMonotonically) {
  TransitionFixture fx;
  fx.sm.loadScene("a.scene.json");
  fx.sm.transitionTo("b.scene.json", TransitionConfig{1.0f});

  fx.sm.tick(0.3f);
  EXPECT_TRUE(fx.sm.isTransitioning());
  fx.sm.tick(0.3f);
  EXPECT_TRUE(fx.sm.isTransitioning());
  fx.sm.tick(0.3f);
  EXPECT_TRUE(fx.sm.isTransitioning());
  fx.sm.tick(0.2f);
  EXPECT_FALSE(fx.sm.isTransitioning());
}

// A tick that lands exactly on the duration finishes the transition (progress
// is clamped to 1.0 and finishTransition fires).
TEST(SceneManager, TransitionFinishesExactlyAtDuration) {
  TransitionFixture fx;
  fx.sm.loadScene("a.scene.json");
  fx.sm.transitionTo("b.scene.json", TransitionConfig{1.0f});

  fx.sm.tick(1.0f);
  EXPECT_FALSE(fx.sm.isTransitioning());
}

// duration <= 0 finishes during the transitionTo call itself; no tick needed
// to clear the transitioning state. Pin the divide-by-zero edge case.
TEST(SceneManager, TransitionDurationZeroFinishesOnFirstTick) {
  TransitionFixture fx;
  fx.sm.loadScene("a.scene.json");

  int finishedCalls = 0;
  fx.bus.subscribe<events::SceneTransitionFinished>(
      EVT_SceneTransitionFinished,
      [&](const events::SceneTransitionFinished&) { ++finishedCalls; });

  fx.sm.transitionTo("b.scene.json", TransitionConfig{0.0f});
  fx.bus.dispatch();

  EXPECT_FALSE(fx.sm.isTransitioning());
  EXPECT_EQ(finishedCalls, 1);

  // Subsequent tick is harmless — already idle.
  fx.sm.tick(0.0f);
  EXPECT_FALSE(fx.sm.isTransitioning());
}

// Calling tick when no transition is in flight does nothing — no events, no
// state mutation. Pins that idle ticks are cheap.
TEST(SceneManager, TickWhenNotTransitioningIsNoOp) {
  TransitionFixture fx;
  fx.sm.loadScene("a.scene.json");
  fx.bus.dispatch();  // drain SceneLoaded from the initial load.

  int unloadCalls = 0, loadedCalls = 0, startedCalls = 0, finishedCalls = 0;
  fx.bus.subscribe<events::SceneUnloading>(
      EVT_SceneUnloading,
      [&](const events::SceneUnloading&) { ++unloadCalls; });
  fx.bus.subscribe<events::SceneLoaded>(
      EVT_SceneLoaded, [&](const events::SceneLoaded&) { ++loadedCalls; });
  fx.bus.subscribe<events::SceneTransitionStarted>(
      EVT_SceneTransitionStarted,
      [&](const events::SceneTransitionStarted&) { ++startedCalls; });
  fx.bus.subscribe<events::SceneTransitionFinished>(
      EVT_SceneTransitionFinished,
      [&](const events::SceneTransitionFinished&) { ++finishedCalls; });

  fx.sm.tick(1000.0f);
  fx.bus.dispatch();

  EXPECT_EQ(unloadCalls, 0);
  EXPECT_EQ(loadedCalls, 0);
  EXPECT_EQ(startedCalls, 0);
  EXPECT_EQ(finishedCalls, 0);
  EXPECT_EQ(fx.sm.getCurrentScenePath(), "a.scene.json");
}

// A complete transition fires Unloading → Loaded → Started → ... → Finished
// in that order. Started fires AFTER the new scene successfully loads (D.7
// reordering) so the Started/Finished pair is symmetric on the happy path
// and absent together on a failed load (where only SceneLoadFailed fires).
TEST(SceneManager, TransitionFiresStartAndFinishEventsInOrder) {
  TransitionFixture fx;
  fx.sm.loadScene("a.scene.json");
  fx.bus.dispatch();  // drain the initial-load events.

  enum class Kind { Started, Unloading, Loaded, Finished };
  std::vector<Kind> seq;
  fx.bus.subscribe<events::SceneTransitionStarted>(
      EVT_SceneTransitionStarted, [&](const events::SceneTransitionStarted&) {
        seq.push_back(Kind::Started);
      });
  fx.bus.subscribe<events::SceneUnloading>(
      EVT_SceneUnloading,
      [&](const events::SceneUnloading&) { seq.push_back(Kind::Unloading); });
  fx.bus.subscribe<events::SceneLoaded>(
      EVT_SceneLoaded,
      [&](const events::SceneLoaded&) { seq.push_back(Kind::Loaded); });
  fx.bus.subscribe<events::SceneTransitionFinished>(
      EVT_SceneTransitionFinished,
      [&](const events::SceneTransitionFinished&) {
        seq.push_back(Kind::Finished);
      });

  fx.sm.transitionTo("b.scene.json", TransitionConfig{0.5f});
  fx.bus.dispatch();
  fx.sm.tick(0.5f);
  fx.bus.dispatch();

  ASSERT_EQ(seq.size(), 4u);
  EXPECT_EQ(seq[0], Kind::Unloading);
  EXPECT_EQ(seq[1], Kind::Loaded);
  EXPECT_EQ(seq[2], Kind::Started);
  EXPECT_EQ(seq[3], Kind::Finished);
}

// SceneTransitionStarted/Finished carry the asset handles for both endpoints.
// Subscribers (e.g. UI overlays) plan against the duration too. The from
// handle is valid here because a scene was loaded prior to the transition.
TEST(SceneManager, TransitionFromInitiallyLoadedSceneCarriesValidFromHandle) {
  TransitionFixture fx;
  AssetHandle handleA = fx.assets.loadAsset("a.scene.json");
  AssetHandle handleB = fx.assets.loadAsset("b.scene.json");

  fx.sm.loadScene("a.scene.json");

  AssetHandle startedFrom, startedTo;
  float startedDuration = -1.0f;
  AssetHandle finishedFrom, finishedTo;
  fx.bus.subscribe<events::SceneTransitionStarted>(
      EVT_SceneTransitionStarted,
      [&](const events::SceneTransitionStarted& e) {
        startedFrom = e.fromScene;
        startedTo = e.toScene;
        startedDuration = e.duration;
      });
  fx.bus.subscribe<events::SceneTransitionFinished>(
      EVT_SceneTransitionFinished,
      [&](const events::SceneTransitionFinished& e) {
        finishedFrom = e.fromScene;
        finishedTo = e.toScene;
      });

  fx.sm.transitionTo("b.scene.json", TransitionConfig{0.75f});
  fx.bus.dispatch();
  fx.sm.tick(0.75f);
  fx.bus.dispatch();

  EXPECT_EQ(startedFrom, handleA);
  EXPECT_EQ(startedTo, handleB);
  EXPECT_FLOAT_EQ(startedDuration, 0.75f);
  EXPECT_EQ(finishedFrom, handleA);
  EXPECT_EQ(finishedTo, handleB);
}

// First transitionTo when no scene was previously loaded uses an invalid
// (default-constructed) AssetHandle as the fromScene sentinel. Pin the
// consistent "no previous scene" behavior — subscribers can rely on
// !fromScene.isValid() to detect the very first transition.
TEST(SceneManager, TransitionFromNoCurrentSceneCarriesInvalidFromHandle) {
  TransitionFixture fx;
  AssetHandle handleA = fx.assets.loadAsset("a.scene.json");

  AssetHandle observedFrom{42};  // sentinel — overwritten by handler
  AssetHandle observedTo;
  fx.bus.subscribe<events::SceneTransitionStarted>(
      EVT_SceneTransitionStarted,
      [&](const events::SceneTransitionStarted& e) {
        observedFrom = e.fromScene;
        observedTo = e.toScene;
      });

  fx.sm.transitionTo("a.scene.json", TransitionConfig{0.5f});
  fx.bus.dispatch();

  EXPECT_FALSE(observedFrom.isValid());
  EXPECT_EQ(observedTo, handleA);
}

// transitionTo unloads + loads synchronously (before any tick). Outgoing
// entities are gone and incoming entities are alive immediately.
TEST(SceneManager, TransitionDestroysOutgoingSceneEntitiesBeforeIncomingLoad) {
  TransitionFixture fx;
  fx.sm.loadScene("a.scene.json");
  ASSERT_EQ(fx.world.findWithTag("a_ent").size(), 1u);

  fx.sm.transitionTo("b.scene.json", TransitionConfig{0.5f});

  EXPECT_TRUE(fx.world.findWithTag("a_ent").empty());
  EXPECT_EQ(fx.world.findWithTag("b_ent").size(), 1u);
}

// Once a transition completes, additional ticks don't re-destroy entities or
// re-fire lifecycle events.
TEST(SceneManager, TransitionTickDoesNotDestroyEntitiesAgain) {
  TransitionFixture fx;
  fx.sm.loadScene("a.scene.json");
  fx.sm.transitionTo("b.scene.json", TransitionConfig{0.5f});
  fx.sm.tick(0.5f);
  fx.bus.dispatch();
  ASSERT_FALSE(fx.sm.isTransitioning());

  int finishedCalls = 0;
  fx.bus.subscribe<events::SceneTransitionFinished>(
      EVT_SceneTransitionFinished,
      [&](const events::SceneTransitionFinished&) { ++finishedCalls; });

  for (int i = 0; i < 5; ++i) {
    fx.sm.tick(0.5f);
  }
  fx.bus.dispatch();

  EXPECT_EQ(finishedCalls, 0);
  EXPECT_EQ(fx.world.findWithTag("b_ent").size(), 1u);
}

// transitionTo while a transition is in flight is REJECTED (logs warning,
// returns without mutating state). Latest-wins replacement was considered
// and rejected — see the plan's transition state-machine notes.
TEST(SceneManager, TransitionToDuringActiveTransitionIsRejected) {
  TransitionFixture fx;
  AssetHandle handleB = fx.assets.loadAsset("b.scene.json");
  fx.sm.loadScene("a.scene.json");

  int finishedCalls = 0;
  AssetHandle finalTo;
  fx.bus.subscribe<events::SceneTransitionFinished>(
      EVT_SceneTransitionFinished,
      [&](const events::SceneTransitionFinished& e) {
        ++finishedCalls;
        finalTo = e.toScene;
      });

  fx.sm.transitionTo("b.scene.json", TransitionConfig{1.0f});
  fx.sm.tick(0.5f);  // halfway through A→B
  ASSERT_TRUE(fx.sm.isTransitioning());
  ASSERT_EQ(fx.world.findWithTag("b_ent").size(), 1u);

  // Attempt to replace mid-flight with B→C — must be rejected.
  fx.sm.transitionTo("c.scene.json", TransitionConfig{1.0f});

  // C never loaded; B still alive; in-flight transition's target is unchanged.
  EXPECT_TRUE(fx.world.findWithTag("c_ent").empty());
  EXPECT_EQ(fx.world.findWithTag("b_ent").size(), 1u);
  EXPECT_EQ(fx.sm.getTransitionState().toScene, handleB);

  fx.sm.tick(1.0f);  // finish A→B
  fx.bus.dispatch();

  EXPECT_FALSE(fx.sm.isTransitioning());
  EXPECT_EQ(finishedCalls, 1);
  EXPECT_EQ(finalTo, handleB);
  EXPECT_EQ(fx.sm.getCurrentScenePath(), "b.scene.json");
}

// loadScene during an active transition is also REJECTED: otherwise the
// renderer's _transitionLive flag stays true with a snapshot referencing
// destroyed entities while the new scene loads behind its back.
TEST(SceneManager, LoadSceneDuringActiveTransitionIsRejected) {
  TransitionFixture fx;
  fx.sm.loadScene("a.scene.json");

  fx.sm.transitionTo("b.scene.json", TransitionConfig{1.0f});
  fx.sm.tick(0.3f);
  ASSERT_TRUE(fx.sm.isTransitioning());

  fx.sm.loadScene("c.scene.json");

  EXPECT_TRUE(fx.world.findWithTag("c_ent").empty());
  EXPECT_EQ(fx.world.findWithTag("b_ent").size(), 1u);
  EXPECT_TRUE(fx.sm.isTransitioning());
  EXPECT_EQ(fx.sm.getCurrentScenePath(), "b.scene.json");

  fx.sm.tick(1.0f);
  EXPECT_FALSE(fx.sm.isTransitioning());
  EXPECT_EQ(fx.sm.getCurrentScenePath(), "b.scene.json");
}

// transitionTo from idle (with a scene already loaded) behaves like a
// load+animate combo: A unloads, B loads, transition runs for the duration.
TEST(SceneManager, TransitionToFromIdleBehavesLikeLoadScenePlusTransition) {
  TransitionFixture fx;
  fx.sm.loadScene("a.scene.json");
  ASSERT_FALSE(fx.sm.isTransitioning());

  fx.sm.transitionTo("b.scene.json", TransitionConfig{0.5f});

  EXPECT_TRUE(fx.sm.isTransitioning());
  EXPECT_TRUE(fx.world.findWithTag("a_ent").empty());
  EXPECT_EQ(fx.world.findWithTag("b_ent").size(), 1u);
  EXPECT_EQ(fx.sm.getCurrentScenePath(), "b.scene.json");
}

// requestLoad defers application until the next tick. State is unchanged
// between request and tick.
TEST(SceneManager, RequestLoadDefersUntilTick) {
  TransitionFixture fx;

  int loadedCalls = 0;
  fx.bus.subscribe<events::SceneLoaded>(
      EVT_SceneLoaded, [&](const events::SceneLoaded&) { ++loadedCalls; });

  fx.sm.requestLoad("a.scene.json");
  EXPECT_TRUE(fx.sm.getCurrentScenePath().empty());
  fx.bus.dispatch();
  EXPECT_EQ(loadedCalls, 0);

  fx.sm.tick(0.0f);
  fx.bus.dispatch();

  EXPECT_EQ(fx.sm.getCurrentScenePath(), "a.scene.json");
  EXPECT_EQ(loadedCalls, 1);
}

// requestTransition defers the transition arming until the next tick. Pin
// that the script-side host function can safely call from a worker thread.
TEST(SceneManager, RequestTransitionDefersUntilTick) {
  TransitionFixture fx;
  fx.sm.loadScene("a.scene.json");

  fx.sm.requestTransition("b.scene.json", TransitionConfig{0.5f});
  EXPECT_FALSE(fx.sm.isTransitioning());
  EXPECT_EQ(fx.sm.getCurrentScenePath(), "a.scene.json");

  fx.sm.tick(0.0f);

  EXPECT_TRUE(fx.sm.isTransitioning());
  EXPECT_EQ(fx.sm.getCurrentScenePath(), "b.scene.json");
}

// Multiple queued requests collapse to the most recent. Intermediate scenes
// never become current — pin that loadScene events for them never fire.
// "Latest-wins" applies to the **pending slot**, NOT to active transitions.
TEST(SceneManager, MultipleQueuedRequestsLatestWinsAtSlot) {
  TransitionFixture fx;

  std::vector<std::string> loadedPaths;
  fx.bus.subscribe<events::SceneLoaded>(
      EVT_SceneLoaded, [&](const events::SceneLoaded& e) {
        loadedPaths.push_back(
            fx.assets.getRawAsset(e.scene).filePath.filename().string());
      });

  fx.sm.requestLoad("a.scene.json");
  fx.sm.requestLoad("b.scene.json");
  fx.sm.requestLoad("c.scene.json");

  fx.sm.tick(0.0f);
  fx.bus.dispatch();

  EXPECT_EQ(fx.sm.getCurrentScenePath(), "c.scene.json");
  ASSERT_EQ(loadedPaths.size(), 1u);
  EXPECT_EQ(loadedPaths[0], "c.scene.json");
}

// Mixed Load + Transition requests: the latest write wins regardless of kind.
// Same caveat — "latest-wins at the pending slot", not "latest-wins replaces
// an active transition".
TEST(SceneManager, MixedLoadAndTransitionRequestsLatestWinsAtSlot) {
  TransitionFixture fx;
  fx.sm.loadScene("a.scene.json");

  fx.sm.requestLoad("a.scene.json");
  fx.sm.requestTransition("b.scene.json", TransitionConfig{0.5f});

  fx.sm.tick(0.0f);

  EXPECT_TRUE(fx.sm.isTransitioning());
  EXPECT_EQ(fx.sm.getCurrentScenePath(), "b.scene.json");
}

// Concurrent requests from many threads must not corrupt the pending slot.
// We don't assert which write wins — only that the state is internally
// consistent and exactly one scene is current after the tick.
TEST(SceneManager, RequestFromMultipleThreadsIsSafe) {
  TransitionFixture fx;

  constexpr int kThreads = 16;
  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  std::atomic<int> ready{0};

  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&, i] {
      // Use a, b, c rotating across threads so the pending slot churns.
      const char* path = (i % 3 == 0)   ? "a.scene.json"
                          : (i % 3 == 1) ? "b.scene.json"
                                         : "c.scene.json";
      ready.fetch_add(1, std::memory_order_relaxed);
      fx.sm.requestLoad(path);
    });
  }

  for (auto& t : threads) t.join();

  fx.sm.tick(0.0f);

  // Whatever path won, it must be one of a/b/c and the manager is in a
  // valid state.
  const std::string current = fx.sm.getCurrentScenePath();
  EXPECT_TRUE(current == "a.scene.json" || current == "b.scene.json" ||
              current == "c.scene.json")
      << "current=" << current;
  EXPECT_FALSE(fx.sm.isTransitioning());
}

// Tick with no pending request and no active transition is a pure no-op.
TEST(SceneManager, TickWithNoPendingRequestIsNoOp) {
  TransitionFixture fx;
  fx.sm.loadScene("a.scene.json");
  fx.bus.dispatch();

  int events = 0;
  auto bump = [&](auto&&) { ++events; };
  fx.bus.subscribe<events::SceneUnloading>(EVT_SceneUnloading, bump);
  fx.bus.subscribe<events::SceneLoaded>(EVT_SceneLoaded, bump);
  fx.bus.subscribe<events::SceneTransitionStarted>(EVT_SceneTransitionStarted,
                                                    bump);
  fx.bus.subscribe<events::SceneTransitionFinished>(EVT_SceneTransitionFinished,
                                                     bump);

  fx.sm.tick(0.5f);
  fx.bus.dispatch();

  EXPECT_EQ(events, 0);
  EXPECT_EQ(fx.sm.getCurrentScenePath(), "a.scene.json");
}

// A request enqueued during an active transition is drained by tick() and
// then DROPPED — the underlying loadScene/transitionTo call rejects because
// a transition is still in flight. Pin: the queue defers but does NOT
// bypass the reject policy. Scripts that care about applying their request
// must guard with Scene.isTransitioning().
TEST(SceneManager, RequestDuringActiveTransitionIsDroppedAtApplyTime) {
  TransitionFixture fx;
  AssetHandle handleB = fx.assets.loadAsset("b.scene.json");

  fx.sm.loadScene("a.scene.json");
  fx.sm.transitionTo("b.scene.json", TransitionConfig{1.0f});
  fx.sm.tick(0.5f);  // halfway through A→B
  fx.bus.dispatch();
  ASSERT_TRUE(fx.sm.isTransitioning());
  ASSERT_EQ(fx.world.findWithTag("b_ent").size(), 1u);

  int extraStartedCalls = 0;
  fx.bus.subscribe<events::SceneTransitionStarted>(
      EVT_SceneTransitionStarted,
      [&](const events::SceneTransitionStarted&) { ++extraStartedCalls; });

  // Queue a transition to C. Next tick drains the queue, attempts
  // transitionTo(C), and that call is rejected — no Started event, no
  // mutation. The A→B transition continues to its own completion.
  fx.sm.requestTransition("c.scene.json", TransitionConfig{1.0f});
  fx.sm.tick(0.0f);
  fx.bus.dispatch();

  EXPECT_EQ(extraStartedCalls, 0);
  EXPECT_TRUE(fx.sm.isTransitioning());
  EXPECT_EQ(fx.sm.getTransitionState().toScene, handleB);
  EXPECT_TRUE(fx.world.findWithTag("c_ent").empty());

  // Run A→B to completion to confirm the in-flight transition wasn't
  // disturbed. Final state = B.
  fx.sm.tick(1.0f);
  EXPECT_FALSE(fx.sm.isTransitioning());
  EXPECT_EQ(fx.sm.getCurrentScenePath(), "b.scene.json");
}

// ---------------------------------------------------------------------------
// D.7: exception-safety hardening.
//
// Failure paths exercised:
//   * SceneLoader rolls back partially-created entities on a mid-load throw.
//   * loadScene + transitionTo emit SceneLoadFailed and re-throw on loader
//     failure; world is left in a clean no-current-scene state.
//   * transitionTo's failure flow does NOT fire SceneTransitionStarted or
//     SceneTransitionFinished — only SceneLoadFailed.
//   * unloadCurrentScene tolerates per-entity destroyEntity exceptions.
//
// Failure is induced by writing a scene with a bad `prefab` reference (a
// path the AssetManager can't resolve). createEntityFromJson calls
// loadAsset on that path, which throws std::runtime_error.
// ---------------------------------------------------------------------------

namespace {

// Build a scene JSON with N entities where the LAST entity references a
// non-existent prefab — the loader processes 0..N-2 fine and throws on N-1.
nlohmann::json sceneWithBadLastEntity(size_t goodCount) {
  nlohmann::json entities = nlohmann::json::array();
  for (size_t i = 0; i < goodCount; ++i) {
    entities.push_back({{"name", "good_" + std::to_string(i)}});
  }
  entities.push_back({{"name", "bad"}, {"prefab", "no-such-prefab.json"}});
  return {{"entities", entities}};
}

// Component whose onDestroy hook throws. Used by the unload-robustness test
// below; defined at namespace scope because COMPONENT_NAME relies on a
// static-inline data member which can't appear inside a local class.
struct ThrowingDestroyComponent : Component<ThrowingDestroyComponent> {
  COMPONENT_NAME("ThrowingDestroyComponent");
};

inline std::atomic<int>& throwingDestroyHookFireCount() {
  static std::atomic<int> count{0};
  return count;
}

inline void throwingDestroyOnDestroyHook(void*) {
  throwingDestroyHookFireCount().fetch_add(1);
  throw std::runtime_error("intentional unload-path throw");
}

}  // namespace

// SceneLoader's parseScene rolls back entities created before the throw, so
// the World is left empty when SceneManager catches the exception. Without
// the rollback those entities would be zombies (alive but not registered
// anywhere SceneManager could destroy them later).
TEST(SceneManager, SceneLoaderRollsBackPartialEntitiesOnComponentFailure) {
  TempDir dir;
  writeScene(dir, "bad.scene.json", sceneWithBadLastEntity(2));

  World world;
  AssetManager assets(dir.path());
  EventBus bus;
  SceneManager sm(world, assets, bus);

  EXPECT_THROW(sm.loadScene("bad.scene.json"), std::runtime_error);

  // The two "good" entities were created before the throw and must be rolled
  // back by SceneLoader. None of the bad-scene entities should remain.
  EXPECT_TRUE(world.findWithTag("good_0").empty());
  EXPECT_TRUE(world.findWithTag("good_1").empty());
  EXPECT_TRUE(world.findWithTag("bad").empty());
}

// loadScene fires SceneLoadFailed exactly once when the loader throws, with
// the AssetHandle of the scene that failed.
TEST(SceneManager, LoadSceneFailureFiresSceneLoadFailedEvent) {
  TempDir dir;
  writeScene(dir, "bad.scene.json", sceneWithBadLastEntity(1));

  World world;
  AssetManager assets(dir.path());
  EventBus bus;
  SceneManager sm(world, assets, bus);
  AssetHandle badHandle = assets.loadAsset("bad.scene.json");

  int failedCalls = 0;
  AssetHandle observed;
  bus.subscribe<events::SceneLoadFailed>(
      EVT_SceneLoadFailed, [&](const events::SceneLoadFailed& e) {
        ++failedCalls;
        observed = e.attempted;
      });

  EXPECT_THROW(sm.loadScene("bad.scene.json"), std::runtime_error);
  bus.dispatch();

  EXPECT_EQ(failedCalls, 1);
  EXPECT_EQ(observed, badHandle);
}

// After a failed load with no prior scene, SceneManager has no current scene
// and the World is empty.
TEST(SceneManager, LoadSceneFailureLeavesNoCurrentScene) {
  TempDir dir;
  writeScene(dir, "bad.scene.json", sceneWithBadLastEntity(2));

  World world;
  AssetManager assets(dir.path());
  EventBus bus;
  SceneManager sm(world, assets, bus);

  EXPECT_THROW(sm.loadScene("bad.scene.json"), std::runtime_error);

  EXPECT_TRUE(sm.getCurrentScenePath().empty());
  EXPECT_FALSE(sm.getCurrentSceneHandle().isValid());
}

// A successful scene followed by a failed scene leaves the World empty: the
// successful scene was unloaded and the failed scene rolled back. The event
// log shows SceneUnloading{A}, SceneLoadFailed{B}, and zero SceneLoaded for
// the failed target.
TEST(SceneManager, LoadSceneFailureAfterSuccessfulLoadReturnsToCleanState) {
  TempDir dir;
  writeScene(dir, "a.scene.json", sceneWithNamedEntities({"a_ent"}));
  writeScene(dir, "bad.scene.json", sceneWithBadLastEntity(1));

  World world;
  AssetManager assets(dir.path());
  EventBus bus;
  SceneManager sm(world, assets, bus);

  AssetHandle handleA = assets.loadAsset("a.scene.json");
  AssetHandle handleBad = assets.loadAsset("bad.scene.json");

  sm.loadScene("a.scene.json");
  bus.dispatch();
  ASSERT_EQ(world.findWithTag("a_ent").size(), 1u);

  AssetHandle unloadedScene;
  AssetHandle failedScene;
  int loadedCalls = 0;
  bus.subscribe<events::SceneUnloading>(
      EVT_SceneUnloading,
      [&](const events::SceneUnloading& e) { unloadedScene = e.scene; });
  bus.subscribe<events::SceneLoadFailed>(
      EVT_SceneLoadFailed,
      [&](const events::SceneLoadFailed& e) { failedScene = e.attempted; });
  bus.subscribe<events::SceneLoaded>(
      EVT_SceneLoaded, [&](const events::SceneLoaded&) { ++loadedCalls; });

  EXPECT_THROW(sm.loadScene("bad.scene.json"), std::runtime_error);
  bus.dispatch();

  EXPECT_EQ(unloadedScene, handleA);
  EXPECT_EQ(failedScene, handleBad);
  EXPECT_EQ(loadedCalls, 0);
  EXPECT_TRUE(world.findWithTag("a_ent").empty());
  EXPECT_TRUE(sm.getCurrentScenePath().empty());
  EXPECT_FALSE(sm.getCurrentSceneHandle().isValid());
}

// transitionTo's failure flow fires SceneLoadFailed but NEVER fires
// SceneTransitionStarted or SceneTransitionFinished — Started/Finished must
// be balanced (both fire on success, neither fires on failure) so subscribers
// can rely on bracket matching.
TEST(SceneManager, TransitionFailureFiresOnlySceneLoadFailedNotStartedOrFinished) {
  TempDir dir;
  writeScene(dir, "a.scene.json", sceneWithNamedEntities({"a_ent"}));
  writeScene(dir, "bad.scene.json", sceneWithBadLastEntity(1));

  World world;
  AssetManager assets(dir.path());
  EventBus bus;
  SceneManager sm(world, assets, bus);

  sm.loadScene("a.scene.json");
  bus.dispatch();

  int startedCalls = 0, finishedCalls = 0, failedCalls = 0;
  int unloadingCalls = 0, loadedCalls = 0;
  bus.subscribe<events::SceneTransitionStarted>(
      EVT_SceneTransitionStarted,
      [&](const events::SceneTransitionStarted&) { ++startedCalls; });
  bus.subscribe<events::SceneTransitionFinished>(
      EVT_SceneTransitionFinished,
      [&](const events::SceneTransitionFinished&) { ++finishedCalls; });
  bus.subscribe<events::SceneLoadFailed>(
      EVT_SceneLoadFailed,
      [&](const events::SceneLoadFailed&) { ++failedCalls; });
  bus.subscribe<events::SceneUnloading>(
      EVT_SceneUnloading,
      [&](const events::SceneUnloading&) { ++unloadingCalls; });
  bus.subscribe<events::SceneLoaded>(
      EVT_SceneLoaded, [&](const events::SceneLoaded&) { ++loadedCalls; });

  EXPECT_THROW(sm.transitionTo("bad.scene.json", TransitionConfig{0.5f}),
               std::runtime_error);
  bus.dispatch();

  EXPECT_EQ(startedCalls, 0);
  EXPECT_EQ(finishedCalls, 0);
  EXPECT_EQ(failedCalls, 1);
  EXPECT_EQ(unloadingCalls, 1);  // outgoing scene was unloaded before the load
  EXPECT_EQ(loadedCalls, 0);     // new scene never finished loading
}

// After a failed transition, SceneManager is back to Phase::Idle — no
// in-flight ActiveTransition lingers.
TEST(SceneManager, TransitionFailureLeavesPhaseIdle) {
  TempDir dir;
  writeScene(dir, "a.scene.json", sceneWithNamedEntities({"a_ent"}));
  writeScene(dir, "bad.scene.json", sceneWithBadLastEntity(1));

  World world;
  AssetManager assets(dir.path());
  EventBus bus;
  SceneManager sm(world, assets, bus);

  sm.loadScene("a.scene.json");

  EXPECT_THROW(sm.transitionTo("bad.scene.json", TransitionConfig{0.5f}),
               std::runtime_error);

  EXPECT_FALSE(sm.isTransitioning());
  EXPECT_FALSE(sm.getTransitionState().active);
}

// Sanity check: a successful transition still fires Started and Finished
// after the D.7 reordering. The new emit order is Unloading → Loaded →
// Started → Finished (Started moved AFTER the load to keep the pair
// symmetric on the failure path).
TEST(SceneManager, TransitionSuccessStillFiresStartedAndFinished) {
  TempDir dir;
  writeScene(dir, "a.scene.json", sceneWithNamedEntities({"a_ent"}));
  writeScene(dir, "b.scene.json", sceneWithNamedEntities({"b_ent"}));

  World world;
  AssetManager assets(dir.path());
  EventBus bus;
  SceneManager sm(world, assets, bus);

  sm.loadScene("a.scene.json");
  bus.dispatch();

  int startedCalls = 0, finishedCalls = 0;
  bus.subscribe<events::SceneTransitionStarted>(
      EVT_SceneTransitionStarted,
      [&](const events::SceneTransitionStarted&) { ++startedCalls; });
  bus.subscribe<events::SceneTransitionFinished>(
      EVT_SceneTransitionFinished,
      [&](const events::SceneTransitionFinished&) { ++finishedCalls; });

  sm.transitionTo("b.scene.json", TransitionConfig{0.5f});
  bus.dispatch();
  EXPECT_EQ(startedCalls, 1);
  EXPECT_EQ(finishedCalls, 0);

  sm.tick(0.5f);
  bus.dispatch();
  EXPECT_EQ(startedCalls, 1);
  EXPECT_EQ(finishedCalls, 1);
}

// Belt-and-suspenders: even if World::destroyEntity were to throw mid-loop
// (it shouldn't, since onDestroy hooks are isolated per-component now),
// SceneManager's unloadCurrentScene must still tear down every owned entity.
//
// We can't easily make destroyEntity itself throw, but we CAN make the
// onDestroy hook throw — World now isolates that, but unloadCurrentScene's
// own try/catch is still valuable defense-in-depth. This test exercises the
// hook-throws path through the full unload, and verifies all entities end
// up destroyed and no exception propagates.
TEST(SceneManager, UnloadHandlesPerEntityDestroyEntityThrow) {
  TempDir dir;

  World world;
  AssetManager assets(dir.path());
  EventBus bus;

  throwingDestroyHookFireCount().store(0);
  world.registerComponent<ThrowingDestroyComponent, char>(
      [](World& w, EntityId id, const nlohmann::json&) {
        w.addComponent<ThrowingDestroyComponent>(id);
      },
      [](const World&, EntityId, nlohmann::json&) { return false; },
      [](World&, EntityId, std::span<const std::byte>) { return false; },
      [](const World&, EntityId, std::span<std::byte>, size_t&) { return false; },
      &throwingDestroyOnDestroyHook);

  // Scene with 3 entities, each carrying the throwing component.
  nlohmann::json entities = nlohmann::json::array();
  for (int i = 0; i < 3; ++i) {
    entities.push_back({
        {"name", "thrower_" + std::to_string(i)},
        {"components", {{"ThrowingDestroyComponent", nlohmann::json::object()}}},
    });
  }
  dir.writeFile("throwers.scene.json",
                nlohmann::json{{"entities", entities}}.dump());
  writeScene(dir, "empty.scene.json", sceneWithNamedEntities({}));

  SceneManager sm(world, assets, bus);
  sm.loadScene("throwers.scene.json");
  ASSERT_EQ(world.findWithTag("thrower_0").size(), 1u);
  ASSERT_EQ(world.findWithTag("thrower_1").size(), 1u);
  ASSERT_EQ(world.findWithTag("thrower_2").size(), 1u);

  // Loading another scene triggers unloadCurrentScene, which must cleanly
  // destroy all three entities even though each fires a throwing hook.
  EXPECT_NO_THROW(sm.loadScene("empty.scene.json"));

  EXPECT_EQ(throwingDestroyHookFireCount().load(), 3);
  EXPECT_TRUE(world.findWithTag("thrower_0").empty());
  EXPECT_TRUE(world.findWithTag("thrower_1").empty());
  EXPECT_TRUE(world.findWithTag("thrower_2").empty());
  EXPECT_EQ(sm.getCurrentScenePath(), "empty.scene.json");
}

// ---------------------------------------------------------------------------
// E.4 — script type converter divergence. In archive mode the wasm bytes are
// inlined into the script entry's payload (the pack bundled them) and imports
// come from resolver metadata. The folder-mode `.script.json` converter
// would JSON-parse the raw payload — a binary wasm header — and throw. These
// tests pin that the type converter handles archive entries directly without
// falling back to the JSON-parsing extension converter.

namespace {

struct ScriptArchiveFixture {
  std::string path;
  std::string type;
  std::vector<std::uint8_t> payload;
  nlohmann::json metadata = nlohmann::json::object();
};

void putU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) {
    out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
  }
}

void putU64(std::vector<std::uint8_t>& out, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
  }
}

std::filesystem::path writeScriptArchive(const TempDir& dir,
                                         const std::string& name,
                                         const std::vector<ScriptArchiveFixture>& entries) {
  nlohmann::json resolver = nlohmann::json::object();
  std::vector<std::uint8_t> payload;
  std::uint64_t offset = 0;
  for (const auto& e : entries) {
    nlohmann::json entry = nlohmann::json::object();
    entry["offset"] = offset;
    entry["size"] = e.payload.size();
    entry["type"] = e.type;
    entry["metadata"] = e.metadata;
    resolver[e.path] = entry;
    payload.insert(payload.end(), e.payload.begin(), e.payload.end());
    offset += e.payload.size();
  }
  const std::string resolverStr = resolver.dump();

  std::vector<std::uint8_t> bytes;
  putU32(bytes, Archive::kMagic);
  putU32(bytes, Archive::kVersion);
  putU64(bytes, Archive::kHeaderSize);
  putU64(bytes, payload.size());
  putU64(bytes, Archive::kHeaderSize + payload.size());
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  bytes.insert(bytes.end(), resolverStr.begin(), resolverStr.end());

  dir.writeFile(name, bytes);
  return dir.path() / name;
}

// Mirrors Engine's converter pair: the .script.json extension converter (which
// would JSON-parse the bytes) and the `script` type converter (which treats
// asset.data as wasm + reads imports from resolver metadata).
void registerEngineStyleScriptConverters(AssetManager& assets, ScriptManager& sm) {
  // Folder-mode: parse JSON, nested-load .wasm, then loadScript.
  assets.addAssetConverter({".script.json"},
      [&assets, &sm](const RawAsset& asset, const AssetHandle& manifestHandle) {
        nlohmann::json manifestJson = nlohmann::json::parse(
            std::string(asset.data.begin(), asset.data.end()));
        std::string wasmPath = manifestJson["binary"].get<std::string>();
        AssetHandle wasmHandle = assets.loadAsset(wasmPath);
        const RawAsset& wasmAsset = assets.getRawAsset(wasmHandle);
        std::vector<std::string> imports;
        if (manifestJson.contains("imports")) {
          imports = manifestJson["imports"].get<std::vector<std::string>>();
        }
        sm.loadScript(manifestHandle, wasmAsset.data, imports);
      });

  // Archive-mode: bytes ARE wasm; imports come from resolver metadata.
  assets.addAssetTypeConverter("script",
      [&assets, &sm](const RawAsset& asset, const AssetHandle& handle) {
        std::vector<std::string> imports;
        auto md = assets.metadataOf(asset.filePath);
        if (md.has_value() && md->contains("imports")) {
          try {
            imports = (*md)["imports"].get<std::vector<std::string>>();
          } catch (const nlohmann::json::exception&) {
            // Continue with empty imports.
          }
        }
        sm.loadScript(handle, asset.data, imports);
      });
}

}  // namespace

// In archive mode the script type converter consumes wasm bytes directly. If
// the .script.json extension converter had fired instead (because type
// dispatch was broken), nlohmann::json::parse would throw on the binary wasm
// header — making this test a regression alarm for "type wins exclusively."
TEST(SceneManager, ArchiveScriptConverterDoesNotCallLoadAssetRecursively) {
  TempDir dir;
  std::vector<std::uint8_t> wasm(std::begin(kMinimalUpdateWasm),
                                 std::end(kMinimalUpdateWasm));
  auto archivePath = writeScriptArchive(
      dir, "game.jm", {{"test.script.json", "script", wasm}});

  AssetManager assets(archivePath);
  ScriptManager sm;
  registerEngineStyleScriptConverters(assets, sm);

  AssetHandle handle;
  ASSERT_NO_THROW(handle = assets.loadAsset("test.script.json"));
  ASSERT_TRUE(handle.isValid());

  // Bytes round-trip through ScriptManager's LoadedScript cache.
  const LoadedScript* loaded = sm.getScript(handle);
  ASSERT_NE(loaded, nullptr);
  EXPECT_EQ(loaded->binary, wasm);
}

// Resolver entry's metadata.imports flows into LoadedScript.imports. Pins
// that imports come from the archive resolver, not from a JSON sidecar.
TEST(SceneManager, ArchiveScriptConverterReadsImportsFromResolverMetadata) {
  TempDir dir;
  std::vector<std::uint8_t> wasm(std::begin(kMinimalUpdateWasm),
                                 std::end(kMinimalUpdateWasm));
  ScriptArchiveFixture entry;
  entry.path = "test.script.json";
  entry.type = "script";
  entry.payload = wasm;
  entry.metadata = nlohmann::json{{"imports", {"abort", "__jmLog"}}};
  auto archivePath = writeScriptArchive(dir, "game.jm", {entry});

  AssetManager assets(archivePath);
  ScriptManager sm;
  registerEngineStyleScriptConverters(assets, sm);

  AssetHandle handle = assets.loadAsset("test.script.json");
  const LoadedScript* loaded = sm.getScript(handle);
  ASSERT_NE(loaded, nullptr);
  ASSERT_EQ(loaded->imports.size(), 2u);
  EXPECT_EQ(loaded->imports[0], "abort");
  EXPECT_EQ(loaded->imports[1], "__jmLog");
}

// An archive script entry with no imports metadata still loads cleanly. The
// LoadedScript ends up with an empty imports list — actual host calls would
// fail at runtime, but the load itself succeeds without throwing.
TEST(SceneManager, ArchiveScriptConverterHandlesMissingMetadataGracefully) {
  TempDir dir;
  std::vector<std::uint8_t> wasm(std::begin(kMinimalUpdateWasm),
                                 std::end(kMinimalUpdateWasm));
  // Default-constructed metadata is `{}` — no "imports" key.
  auto archivePath = writeScriptArchive(
      dir, "game.jm", {{"test.script.json", "script", wasm}});

  AssetManager assets(archivePath);
  ScriptManager sm;
  registerEngineStyleScriptConverters(assets, sm);

  AssetHandle handle;
  ASSERT_NO_THROW(handle = assets.loadAsset("test.script.json"));
  const LoadedScript* loaded = sm.getScript(handle);
  ASSERT_NE(loaded, nullptr);
  EXPECT_TRUE(loaded->imports.empty());
}
