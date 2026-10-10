#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <string>
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

// Scenes a, b and c each hold one entity tagged a_ent, b_ent, c_ent.
class SceneManagerTest : public ::testing::Test {
 protected:
  SceneManagerTest() {
    for (const char* name : {"a", "b", "c"}) {
      writeScene(dir, std::string(name) + ".scene.json", sceneWithNamedEntities({std::string(name) + "_ent"}));
    }
  }

  TempDir dir;
  World world;
  AssetManager assets{dir.path()};
  EventBus bus;
  SceneManager sm{world, assets, bus};
};

}  // namespace

// loadScene fires SceneLoaded once with the AssetHandle for the loaded path.
TEST_F(SceneManagerTest, LoadSceneFiresSceneLoaded) {
  writeScene(dir, "level.scene.json", sceneWithNamedEntities({"player"}));

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

// Loading two distinct scenes back-to-back fires events in this order:
// SceneLoaded{A} → SceneUnloading{A} → SceneLoaded{B}.
TEST_F(SceneManagerTest, LoadSceneTwiceFiresUnloadingAndLoadedInOrder) {
  writeScene(dir, "a.scene.json", sceneWithNamedEntities({"a_ent"}));
  writeScene(dir, "b.scene.json", sceneWithNamedEntities({"b_ent"}));

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

// Reloading the same scene path tears the old instance down and rebuilds it.
TEST_F(SceneManagerTest, ReloadingSameScenePathStillUnloadsFirst) {
  writeScene(dir, "level.scene.json", sceneWithNamedEntities({"thing"}));

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
TEST_F(SceneManagerTest, UnloadDestroysEveryEntityFromPreviousScene) {
  writeScene(dir, "a.scene.json",
             sceneWithNamedEntities({"alpha", "beta", "gamma"}));
  writeScene(dir, "b.scene.json", sceneWithNamedEntities({}));

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

// A scene without an "entities" key is an empty scene.
TEST_F(SceneManagerTest, LoadSceneWithNoEntitiesFieldDoesNotCrash) {
  dir.writeFile("noEntities.scene.json",
                nlohmann::json{{"name", "scene_without_entities"}}.dump());

  EXPECT_NO_THROW(sm.loadScene("noEntities.scene.json"));
  EXPECT_EQ(sm.getCurrentScenePath(), "noEntities.scene.json");
}

// Entities created via World::createEntity (NOT through SceneManager) must
// survive a scene swap — SceneManager only owns what SceneLoader produced.
TEST_F(SceneManagerTest, EntitiesCreatedOutsideSceneLoadAreNotOwned) {
  writeScene(dir, "a.scene.json", sceneWithNamedEntities({"a_ent"}));
  writeScene(dir, "b.scene.json", sceneWithNamedEntities({"b_ent"}));

  sm.loadScene("a.scene.json");
  EntityId external = world.createEntity("external");
  ASSERT_TRUE(world.isAlive(external));

  sm.loadScene("b.scene.json");

  EXPECT_TRUE(world.isAlive(external));
  EXPECT_EQ(world.findWithTag("external").size(), 1u);
  EXPECT_TRUE(world.findWithTag("a_ent").empty());
}

// A failed load (missing file) propagates the AssetManager exception and
// leaves the previously loaded scene untouched.
TEST_F(SceneManagerTest, LoadSceneWithMissingFileThrows) {
  writeScene(dir, "good.scene.json", sceneWithNamedEntities({"keeper"}));

  sm.loadScene("good.scene.json");
  ASSERT_EQ(world.findWithTag("keeper").size(), 1u);

  EXPECT_THROW(sm.loadScene("does-not-exist.scene.json"), std::runtime_error);

  EXPECT_EQ(sm.getCurrentScenePath(), "good.scene.json");
  EXPECT_EQ(world.findWithTag("keeper").size(), 1u);
}

// Unloading a scene releases its entities' script instances (ScriptComponent onDestroy).

namespace {

// The smallest wasm ScriptInstance accepts: an empty exported `onUpdate(f32)`.
// Hand-encoded, so the tests need no wasm toolchain.
constexpr uint8_t kMinimalUpdateWasm[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x05, 0x01, 0x60, 0x01, 0x7d, 0x00,
    0x03, 0x02, 0x01, 0x00,
    0x07, 0x0c, 0x01, 0x08, 0x6f, 0x6e, 0x55, 0x70, 0x64, 0x61, 0x74, 0x65,
    0x00, 0x00,
    0x0a, 0x04, 0x01, 0x02, 0x00, 0x0b,
};

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
        sm.loadScript(manifestHandle, wasmAsset.data);
      });

  return scriptJsonRel;
}

// Register ScriptComponent on `world` with the destroy hook. The deserializer
// reads `script` as a path, loads the asset (which triggers the converter
// registered via stageScriptAsset), creates an instance, and attaches the
// component.
void registerScriptComponentForTest(World& world, AssetManager& assets,
                                    ScriptManager& sm) {
  world.registerComponent<ScriptComponent>({
      .fromJson =
          [&assets, &sm](ScriptComponent& c, const nlohmann::json& json, EntityId id) {
            c.started = true;
            c.instance = sm.createInstance(assets.loadAsset(json["script"].get<std::string>()), id);
          },
      .onDestroy = [&sm](ScriptComponent& c) { sm.destroyInstance(c.instance); },
  });
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
}

// Bouncing between a scripted scene and an empty one never accumulates instances.
TEST(SceneManager, RepeatedSceneSwapsDoNotLeakWasmInstances) {
  TempDir dir;

  World world;
  AssetManager assets(dir.path());
  EventBus bus;
  ScriptManager scriptManager;

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
}

// transitionTo arms the state machine: isTransitioning is true after the call,
// remains true while elapsed < duration, flips false once the duration is
// reached.
TEST_F(SceneManagerTest, IsTransitioningTrueDuringTransition) {
  sm.loadScene("a.scene.json");
  EXPECT_FALSE(sm.isTransitioning());

  sm.transitionTo("b.scene.json", TransitionConfig{0.5f});
  EXPECT_TRUE(sm.isTransitioning());

  sm.tick(0.5f + 0.001f);
  EXPECT_FALSE(sm.isTransitioning());
}

// Successive ticks add up — only the tick that crosses the duration boundary
// finishes the transition.
TEST_F(SceneManagerTest, TransitionAdvancesMonotonically) {
  sm.loadScene("a.scene.json");
  sm.transitionTo("b.scene.json", TransitionConfig{1.0f});

  sm.tick(0.3f);
  EXPECT_TRUE(sm.isTransitioning());
  sm.tick(0.3f);
  EXPECT_TRUE(sm.isTransitioning());
  sm.tick(0.3f);
  EXPECT_TRUE(sm.isTransitioning());
  sm.tick(0.2f);
  EXPECT_FALSE(sm.isTransitioning());
}

// A tick that lands exactly on the duration finishes the transition (progress
// is clamped to 1.0 and finishTransition fires).
TEST_F(SceneManagerTest, TransitionFinishesExactlyAtDuration) {
  sm.loadScene("a.scene.json");
  sm.transitionTo("b.scene.json", TransitionConfig{1.0f});

  sm.tick(1.0f);
  EXPECT_FALSE(sm.isTransitioning());
}

// duration <= 0 finishes inside transitionTo itself (no divide by zero).
TEST_F(SceneManagerTest, TransitionDurationZeroFinishesOnFirstTick) {
  sm.loadScene("a.scene.json");

  int finishedCalls = 0;
  bus.subscribe<events::SceneTransitionFinished>(
      EVT_SceneTransitionFinished,
      [&](const events::SceneTransitionFinished&) { ++finishedCalls; });

  sm.transitionTo("b.scene.json", TransitionConfig{0.0f});
  bus.dispatch();

  EXPECT_FALSE(sm.isTransitioning());
  EXPECT_EQ(finishedCalls, 1);

  // Subsequent tick is harmless — already idle.
  sm.tick(0.0f);
  EXPECT_FALSE(sm.isTransitioning());
}

// A transition fires Unloading, Loaded, Started, Finished: Started comes after
// the load, so a failed load fires neither Started nor Finished.
TEST_F(SceneManagerTest, TransitionFiresStartAndFinishEventsInOrder) {
  sm.loadScene("a.scene.json");
  bus.dispatch();  // drain the initial-load events.

  enum class Kind { Started, Unloading, Loaded, Finished };
  std::vector<Kind> seq;
  bus.subscribe<events::SceneTransitionStarted>(
      EVT_SceneTransitionStarted, [&](const events::SceneTransitionStarted&) {
        seq.push_back(Kind::Started);
      });
  bus.subscribe<events::SceneUnloading>(
      EVT_SceneUnloading,
      [&](const events::SceneUnloading&) { seq.push_back(Kind::Unloading); });
  bus.subscribe<events::SceneLoaded>(
      EVT_SceneLoaded,
      [&](const events::SceneLoaded&) { seq.push_back(Kind::Loaded); });
  bus.subscribe<events::SceneTransitionFinished>(
      EVT_SceneTransitionFinished,
      [&](const events::SceneTransitionFinished&) {
        seq.push_back(Kind::Finished);
      });

  sm.transitionTo("b.scene.json", TransitionConfig{0.5f});
  bus.dispatch();
  sm.tick(0.5f);
  bus.dispatch();

  ASSERT_EQ(seq.size(), 4u);
  EXPECT_EQ(seq[0], Kind::Unloading);
  EXPECT_EQ(seq[1], Kind::Loaded);
  EXPECT_EQ(seq[2], Kind::Started);
  EXPECT_EQ(seq[3], Kind::Finished);
}

// Started/Finished carry both endpoints' handles, and Started the duration.
TEST_F(SceneManagerTest, TransitionFromInitiallyLoadedSceneCarriesValidFromHandle) {
  AssetHandle handleA = assets.loadAsset("a.scene.json");
  AssetHandle handleB = assets.loadAsset("b.scene.json");

  sm.loadScene("a.scene.json");

  AssetHandle startedFrom, startedTo;
  float startedDuration = -1.0f;
  AssetHandle finishedFrom, finishedTo;
  bus.subscribe<events::SceneTransitionStarted>(
      EVT_SceneTransitionStarted,
      [&](const events::SceneTransitionStarted& e) {
        startedFrom = e.fromScene;
        startedTo = e.toScene;
        startedDuration = e.duration;
      });
  bus.subscribe<events::SceneTransitionFinished>(
      EVT_SceneTransitionFinished,
      [&](const events::SceneTransitionFinished& e) {
        finishedFrom = e.fromScene;
        finishedTo = e.toScene;
      });

  sm.transitionTo("b.scene.json", TransitionConfig{0.75f});
  bus.dispatch();
  sm.tick(0.75f);
  bus.dispatch();

  EXPECT_EQ(startedFrom, handleA);
  EXPECT_EQ(startedTo, handleB);
  EXPECT_FLOAT_EQ(startedDuration, 0.75f);
  EXPECT_EQ(finishedFrom, handleA);
  EXPECT_EQ(finishedTo, handleB);
}

// With no previous scene, fromScene is an invalid handle.
TEST_F(SceneManagerTest, TransitionFromNoCurrentSceneCarriesInvalidFromHandle) {
  AssetHandle handleA = assets.loadAsset("a.scene.json");

  AssetHandle observedFrom{42};  // sentinel — overwritten by handler
  AssetHandle observedTo;
  bus.subscribe<events::SceneTransitionStarted>(
      EVT_SceneTransitionStarted,
      [&](const events::SceneTransitionStarted& e) {
        observedFrom = e.fromScene;
        observedTo = e.toScene;
      });

  sm.transitionTo("a.scene.json", TransitionConfig{0.5f});
  bus.dispatch();

  EXPECT_FALSE(observedFrom.isValid());
  EXPECT_EQ(observedTo, handleA);
}

// transitionTo unloads + loads synchronously (before any tick). Outgoing
// entities are gone and incoming entities are alive immediately.
TEST_F(SceneManagerTest, TransitionDestroysOutgoingSceneEntitiesBeforeIncomingLoad) {
  sm.loadScene("a.scene.json");
  ASSERT_EQ(world.findWithTag("a_ent").size(), 1u);

  sm.transitionTo("b.scene.json", TransitionConfig{0.5f});

  EXPECT_TRUE(world.findWithTag("a_ent").empty());
  EXPECT_EQ(world.findWithTag("b_ent").size(), 1u);
}

// Once a transition completes, additional ticks don't re-destroy entities or
// re-fire lifecycle events.
TEST_F(SceneManagerTest, TransitionTickDoesNotDestroyEntitiesAgain) {
  sm.loadScene("a.scene.json");
  sm.transitionTo("b.scene.json", TransitionConfig{0.5f});
  sm.tick(0.5f);
  bus.dispatch();
  ASSERT_FALSE(sm.isTransitioning());

  int finishedCalls = 0;
  bus.subscribe<events::SceneTransitionFinished>(
      EVT_SceneTransitionFinished,
      [&](const events::SceneTransitionFinished&) { ++finishedCalls; });

  for (int i = 0; i < 5; ++i) {
    sm.tick(0.5f);
  }
  bus.dispatch();

  EXPECT_EQ(finishedCalls, 0);
  EXPECT_EQ(world.findWithTag("b_ent").size(), 1u);
}

// transitionTo while a transition is in flight is rejected, changing nothing.
TEST_F(SceneManagerTest, TransitionToDuringActiveTransitionIsRejected) {
  AssetHandle handleB = assets.loadAsset("b.scene.json");
  sm.loadScene("a.scene.json");

  int finishedCalls = 0;
  AssetHandle finalTo;
  bus.subscribe<events::SceneTransitionFinished>(
      EVT_SceneTransitionFinished,
      [&](const events::SceneTransitionFinished& e) {
        ++finishedCalls;
        finalTo = e.toScene;
      });

  sm.transitionTo("b.scene.json", TransitionConfig{1.0f});
  sm.tick(0.5f);  // halfway through A→B
  ASSERT_TRUE(sm.isTransitioning());
  ASSERT_EQ(world.findWithTag("b_ent").size(), 1u);

  // Attempt to replace mid-flight with B→C — must be rejected.
  sm.transitionTo("c.scene.json", TransitionConfig{1.0f});

  // C never loaded; B still alive; in-flight transition's target is unchanged.
  EXPECT_TRUE(world.findWithTag("c_ent").empty());
  EXPECT_EQ(world.findWithTag("b_ent").size(), 1u);
  EXPECT_EQ(sm.getCurrentSceneHandle(), handleB);

  sm.tick(1.0f);  // finish A→B
  bus.dispatch();

  EXPECT_FALSE(sm.isTransitioning());
  EXPECT_EQ(finishedCalls, 1);
  EXPECT_EQ(finalTo, handleB);
  EXPECT_EQ(sm.getCurrentScenePath(), "b.scene.json");
}

// loadScene during a transition is rejected too: the renderer's snapshot
// would outlive the scene it composites.
TEST_F(SceneManagerTest, LoadSceneDuringActiveTransitionIsRejected) {
  sm.loadScene("a.scene.json");

  sm.transitionTo("b.scene.json", TransitionConfig{1.0f});
  sm.tick(0.3f);
  ASSERT_TRUE(sm.isTransitioning());

  sm.loadScene("c.scene.json");

  EXPECT_TRUE(world.findWithTag("c_ent").empty());
  EXPECT_EQ(world.findWithTag("b_ent").size(), 1u);
  EXPECT_TRUE(sm.isTransitioning());
  EXPECT_EQ(sm.getCurrentScenePath(), "b.scene.json");

  sm.tick(1.0f);
  EXPECT_FALSE(sm.isTransitioning());
  EXPECT_EQ(sm.getCurrentScenePath(), "b.scene.json");
}

// requestLoad defers application until the next tick. State is unchanged
// between request and tick.
TEST_F(SceneManagerTest, RequestLoadDefersUntilTick) {
  int loadedCalls = 0;
  bus.subscribe<events::SceneLoaded>(
      EVT_SceneLoaded, [&](const events::SceneLoaded&) { ++loadedCalls; });

  sm.requestLoad("a.scene.json");
  EXPECT_TRUE(sm.getCurrentScenePath().empty());
  bus.dispatch();
  EXPECT_EQ(loadedCalls, 0);

  sm.tick(0.0f);
  bus.dispatch();

  EXPECT_EQ(sm.getCurrentScenePath(), "a.scene.json");
  EXPECT_EQ(loadedCalls, 1);
}

// A script's request for a missing scene is logged, not thrown out of the frame.
TEST_F(SceneManagerTest, RequestedLoadOfMissingSceneKeepsCurrentScene) {
  sm.loadScene("a.scene.json");
  sm.requestTransition("missing.scene.json", TransitionConfig{std::nanf("")});
  EXPECT_NO_THROW(sm.tick(0.0f));
  EXPECT_EQ(sm.getCurrentScenePath(), "a.scene.json");

  // A NaN duration finishes at once instead of blocking every later change.
  sm.transitionTo("b.scene.json", TransitionConfig{std::nanf("")});
  EXPECT_FALSE(sm.isTransitioning());
}

// requestTransition defers the transition arming until the next tick. Pin
// that the script-side host function can safely call from a worker thread.
TEST_F(SceneManagerTest, RequestTransitionDefersUntilTick) {
  sm.loadScene("a.scene.json");

  sm.requestTransition("b.scene.json", TransitionConfig{0.5f});
  EXPECT_FALSE(sm.isTransitioning());
  EXPECT_EQ(sm.getCurrentScenePath(), "a.scene.json");

  sm.tick(0.0f);

  EXPECT_TRUE(sm.isTransitioning());
  EXPECT_EQ(sm.getCurrentScenePath(), "b.scene.json");
}

// Queued requests collapse to the most recent; intermediate scenes never load.
TEST_F(SceneManagerTest, MultipleQueuedRequestsLatestWinsAtSlot) {
  std::vector<std::string> loadedPaths;
  bus.subscribe<events::SceneLoaded>(
      EVT_SceneLoaded, [&](const events::SceneLoaded& e) {
        loadedPaths.push_back(
            assets.getRawAsset(e.scene).filePath.filename().string());
      });

  sm.requestLoad("a.scene.json");
  sm.requestLoad("b.scene.json");
  sm.requestLoad("c.scene.json");

  sm.tick(0.0f);
  bus.dispatch();

  EXPECT_EQ(sm.getCurrentScenePath(), "c.scene.json");
  ASSERT_EQ(loadedPaths.size(), 1u);
  EXPECT_EQ(loadedPaths[0], "c.scene.json");
}

// The latest request wins whatever its kind.
TEST_F(SceneManagerTest, MixedLoadAndTransitionRequestsLatestWinsAtSlot) {
  sm.loadScene("a.scene.json");

  sm.requestLoad("a.scene.json");
  sm.requestTransition("b.scene.json", TransitionConfig{0.5f});

  sm.tick(0.0f);

  EXPECT_TRUE(sm.isTransitioning());
  EXPECT_EQ(sm.getCurrentScenePath(), "b.scene.json");
}

// Tick with no pending request and no active transition is a pure no-op.
TEST_F(SceneManagerTest, TickWithNoPendingRequestIsNoOp) {
  sm.loadScene("a.scene.json");
  bus.dispatch();

  int events = 0;
  auto bump = [&](auto&&) { ++events; };
  bus.subscribe<events::SceneUnloading>(EVT_SceneUnloading, bump);
  bus.subscribe<events::SceneLoaded>(EVT_SceneLoaded, bump);
  bus.subscribe<events::SceneTransitionStarted>(EVT_SceneTransitionStarted,
                                                    bump);
  bus.subscribe<events::SceneTransitionFinished>(EVT_SceneTransitionFinished,
                                                     bump);

  sm.tick(0.5f);
  bus.dispatch();

  EXPECT_EQ(events, 0);
  EXPECT_EQ(sm.getCurrentScenePath(), "a.scene.json");
}

// A request made during a transition waits for it to finish, then applies.
TEST_F(SceneManagerTest, RequestDuringActiveTransitionAppliesAfterIt) {
  AssetHandle handleB = assets.loadAsset("b.scene.json");

  sm.loadScene("a.scene.json");
  sm.transitionTo("b.scene.json", TransitionConfig{1.0f});
  sm.tick(0.5f);  // halfway through A→B
  bus.dispatch();
  ASSERT_TRUE(sm.isTransitioning());

  sm.requestTransition("c.scene.json", TransitionConfig{1.0f});
  sm.tick(0.0f);
  EXPECT_EQ(sm.getCurrentSceneHandle(), handleB);
  EXPECT_TRUE(world.findWithTag("c_ent").empty());

  sm.tick(1.0f);  // A→B ends
  EXPECT_EQ(sm.getCurrentScenePath(), "b.scene.json");
  sm.tick(0.0f);  // the waiting request starts B→C
  EXPECT_TRUE(sm.isTransitioning());
  EXPECT_EQ(sm.getCurrentScenePath(), "c.scene.json");
  EXPECT_EQ(world.findWithTag("c_ent").size(), 1u);
}

// Failure paths. A bad `prefab` reference makes createEntityFromJson throw.

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

// A failed load rolls back the entities it made before the throw.
TEST_F(SceneManagerTest, SceneLoaderRollsBackPartialEntitiesOnComponentFailure) {
  writeScene(dir, "bad.scene.json", sceneWithBadLastEntity(2));

  EXPECT_THROW(sm.loadScene("bad.scene.json"), std::runtime_error);

  // The two "good" entities were created before the throw and must be rolled
  // back by SceneLoader. None of the bad-scene entities should remain.
  EXPECT_TRUE(world.findWithTag("good_0").empty());
  EXPECT_TRUE(world.findWithTag("good_1").empty());
  EXPECT_TRUE(world.findWithTag("bad").empty());
}

// loadScene fires SceneLoadFailed exactly once when the loader throws, with
// the AssetHandle of the scene that failed.
TEST_F(SceneManagerTest, LoadSceneFailureFiresSceneLoadFailedEvent) {
  writeScene(dir, "bad.scene.json", sceneWithBadLastEntity(1));

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

// A retry that works is one load: SceneLoaded only (multiplayer starts a scene once per event).
TEST_F(SceneManagerTest, ALoadThatWorksOnItsRetryIsOneLoad) {
  writeScene(dir, "bad.scene.json", sceneWithBadLastEntity(1));
  int failed = 0, loaded = 0, retries = 0;
  bus.subscribe<events::SceneLoadFailed>(EVT_SceneLoadFailed, [&](const events::SceneLoadFailed&) { ++failed; });
  bus.subscribe<events::SceneLoaded>(EVT_SceneLoaded, [&](const events::SceneLoaded&) { ++loaded; });
  sm.loadScene("bad.scene.json", [&](const std::exception&) {
    ++retries;
    const auto file = dir.path() / "bad.scene.json";
    writeScene(dir, "bad.scene.json", sceneWithNamedEntities({"fixed"}));
    std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) + std::chrono::seconds(2));
    assets.reloadChanged();
  });
  bus.dispatch();
  EXPECT_EQ(retries, 1);
  EXPECT_EQ(failed, 0);
  EXPECT_EQ(loaded, 1);
  EXPECT_EQ(sm.getCurrentScenePath(), "bad.scene.json");

  writeScene(dir, "worse.scene.json", sceneWithBadLastEntity(1));  // still bad on the retry
  EXPECT_THROW(sm.loadScene("worse.scene.json", [](const std::exception&) {}), std::runtime_error);
  bus.dispatch();
  EXPECT_EQ(failed, 1);
}

// After a failed load with no prior scene, SceneManager has no current scene
// and the World is empty.
TEST_F(SceneManagerTest, LoadSceneFailureLeavesNoCurrentScene) {
  writeScene(dir, "bad.scene.json", sceneWithBadLastEntity(2));

  EXPECT_THROW(sm.loadScene("bad.scene.json"), std::runtime_error);

  EXPECT_TRUE(sm.getCurrentScenePath().empty());
  EXPECT_FALSE(sm.getCurrentSceneHandle().isValid());
}

// A successful scene followed by a failed scene leaves the World empty: the
// successful scene was unloaded and the failed scene rolled back. The event
// log shows SceneUnloading{A}, SceneLoadFailed{B}, and zero SceneLoaded for
// the failed target.
TEST_F(SceneManagerTest, LoadSceneFailureAfterSuccessfulLoadReturnsToCleanState) {
  writeScene(dir, "a.scene.json", sceneWithNamedEntities({"a_ent"}));
  writeScene(dir, "bad.scene.json", sceneWithBadLastEntity(1));

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
TEST_F(SceneManagerTest, TransitionFailureFiresOnlySceneLoadFailedNotStartedOrFinished) {
  writeScene(dir, "a.scene.json", sceneWithNamedEntities({"a_ent"}));
  writeScene(dir, "bad.scene.json", sceneWithBadLastEntity(1));

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
  EXPECT_FALSE(sm.isTransitioning());
}

// Unload destroys every entity even when each one's destroy hook throws.
TEST_F(SceneManagerTest, UnloadHandlesPerEntityDestroyEntityThrow) {
  throwingDestroyHookFireCount().store(0);
  world.registerComponent<ThrowingDestroyComponent>(
      {.onDestroy = [](ThrowingDestroyComponent&) { throwingDestroyOnDestroyHook(nullptr); }});

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

  sm.loadScene("throwers.scene.json");
  ASSERT_EQ(world.findWithTag("thrower_0").size(), 1u);
  ASSERT_EQ(world.findWithTag("thrower_1").size(), 1u);
  ASSERT_EQ(world.findWithTag("thrower_2").size(), 1u);

  EXPECT_NO_THROW(sm.loadScene("empty.scene.json"));

  EXPECT_EQ(throwingDestroyHookFireCount().load(), 3);
  EXPECT_TRUE(world.findWithTag("thrower_0").empty());
  EXPECT_TRUE(world.findWithTag("thrower_1").empty());
  EXPECT_TRUE(world.findWithTag("thrower_2").empty());
  EXPECT_EQ(sm.getCurrentScenePath(), "empty.scene.json");
}

// Archives inline a script's wasm in its entry; the `script` type converter
// must take it, not the folder-mode `.script.json` converter (which would
// JSON-parse the wasm and throw).

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

// Mirrors Engine's converter pair: folder-mode `.script.json` and archive-mode `script`.
void registerEngineStyleScriptConverters(AssetManager& assets, ScriptManager& sm) {
  // Folder-mode: parse JSON, nested-load .wasm, then loadScript.
  assets.addAssetConverter({".script.json"},
      [&assets, &sm](const RawAsset& asset, const AssetHandle& manifestHandle) {
        nlohmann::json manifestJson = nlohmann::json::parse(
            std::string(asset.data.begin(), asset.data.end()));
        std::string wasmPath = manifestJson["binary"].get<std::string>();
        AssetHandle wasmHandle = assets.loadAsset(wasmPath);
        const RawAsset& wasmAsset = assets.getRawAsset(wasmHandle);
        sm.loadScript(manifestHandle, wasmAsset.data);
      });

  // Archive-mode: bytes ARE wasm.
  assets.addAssetTypeConverter("script",
      [&sm](const RawAsset& asset, const AssetHandle& handle) {
        sm.loadScript(handle, asset.data);
      });
}

}  // namespace

// An archive entry (no metadata) loads through the type converter.
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

// Resolver metadata.imports survives for inspection tools (the engine itself ignores it).
TEST(SceneManager, ArchiveScriptConverterPreservesMetadataInResolver) {
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
  EXPECT_EQ(loaded->binary, wasm);

  auto md = assets.metadataOf("test.script.json");
  ASSERT_TRUE(md.has_value());
  ASSERT_TRUE(md->contains("imports"));
  auto imports = (*md)["imports"].get<std::vector<std::string>>();
  ASSERT_EQ(imports.size(), 2u);
  EXPECT_EQ(imports[0], "abort");
  EXPECT_EQ(imports[1], "__jmLog");
}

// Grouped entries wait for spawnGroup; despawn removes them and a later spawn
// starts the group afresh. "if"/"unless" consult the condition each time.
TEST_F(SceneManagerTest, GroupsSpawnOnRequestAndConditionsFilterEntries) {
  nlohmann::json scene = {{"entities", nlohmann::json::array({
      {{"name", "always"}},
      {{"name", "collected"}, {"unless", "done.key"}},
      {{"name", "slime"}, {"group", "room"}},
      {{"name", "shard"}, {"group", "room"}, {"if", "done.boss"}},
  })}};
  writeScene(dir, "level.scene.json", scene);

  bool bossDone = false;
  sm.setCondition([&](const std::string& key) { return key == "done.key" || (key == "done.boss" && bossDone); });
  sm.loadScene("level.scene.json");

  auto count = [&](const char* tag) { return world.findWithTag(tag).size(); };
  EXPECT_EQ(count("always"), 1u);
  EXPECT_EQ(count("collected"), 0u);  // its key is set
  EXPECT_EQ(count("slime"), 0u);      // waiting in its group

  sm.spawnGroup("room");
  sm.spawnGroup("room");  // already spawned: no duplicates
  EXPECT_EQ(count("slime"), 1u);
  EXPECT_EQ(count("shard"), 0u);
  EXPECT_TRUE(sm.groupSpawned("room"));

  sm.despawnGroup("room");
  EXPECT_EQ(count("slime"), 0u);
  bossDone = true;
  sm.spawnGroup("room");
  EXPECT_EQ(count("shard"), 1u);
}
