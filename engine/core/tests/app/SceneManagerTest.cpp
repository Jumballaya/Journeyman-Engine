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

// A complete transition fires Started → Unloading → Loaded → ... → Finished
// in that order. The renderer relies on Started/Finished as the bracketing
// events; gameplay scripts use the inner Loaded.
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
  EXPECT_EQ(seq[0], Kind::Started);
  EXPECT_EQ(seq[1], Kind::Unloading);
  EXPECT_EQ(seq[2], Kind::Loaded);
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
