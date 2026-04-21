#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../assets/TempDir.hpp"
#include "AssetManager.hpp"
#include "SceneLoader.hpp"
#include "World.hpp"
#include "component/Component.hpp"
#include "prefab/Prefab.hpp"
#include "prefab/PrefabLoader.hpp"

namespace {

struct PrefabPosition : Component<PrefabPosition> {
  COMPONENT_NAME("PrefabPosition");
  float x = 0.0f;
  float y = 0.0f;
};

struct PrefabVelocity : Component<PrefabVelocity> {
  COMPONENT_NAME("PrefabVelocity");
  float dx = 0.0f;
  float dy = 0.0f;
};

void registerPrefabPosition(World &world) {
  world.registerComponent<PrefabPosition, PrefabPosition>(
      [](World &w, EntityId id, const nlohmann::json &j) {
        PrefabPosition p;
        if (j.contains("x"))
          p.x = j["x"].get<float>();
        if (j.contains("y"))
          p.y = j["y"].get<float>();
        w.addComponent<PrefabPosition>(id, p);
      },
      [](const World &, EntityId, nlohmann::json &) { return false; },
      [](World &, EntityId, std::span<const std::byte>) { return false; },
      [](const World &, EntityId, std::span<std::byte>, size_t &) {
        return false;
      });
}

const nlohmann::json *findComponent(const Prefab &prefab,
                                    std::string_view name) {
  for (const auto &[n, j] : prefab.components) {
    if (n == name)
      return &j;
  }
  return nullptr;
}

struct PrefabBoom : Component<PrefabBoom> {
  COMPONENT_NAME("PrefabBoom");
};

void registerPrefabBoom(World &world) {
  world.registerComponent<PrefabBoom, PrefabBoom>(
      [](World &, EntityId, const nlohmann::json &) {
        throw std::runtime_error("boom");
      },
      [](const World &, EntityId, nlohmann::json &) { return false; },
      [](World &, EntityId, std::span<const std::byte>) { return false; },
      [](const World &, EntityId, std::span<std::byte>, size_t &) {
        return false;
      });
}

void registerPrefabVelocity(World &world) {
  world.registerComponent<PrefabVelocity, PrefabVelocity>(
      [](World &w, EntityId id, const nlohmann::json &j) {
        PrefabVelocity v;
        if (j.contains("dx"))
          v.dx = j["dx"].get<float>();
        if (j.contains("dy"))
          v.dy = j["dy"].get<float>();
        w.addComponent<PrefabVelocity>(id, v);
      },
      [](const World &, EntityId, nlohmann::json &) { return false; },
      [](World &, EntityId, std::span<const std::byte>) { return false; },
      [](const World &, EntityId, std::span<std::byte>, size_t &) {
        return false;
      });
}

} // namespace

// PrefabLoader pulls the components map and tags array out of a JSON object.
TEST(PrefabLoader, ParsesComponentsAndTags) {
  nlohmann::json j = {{"components",
                       {{"PrefabPosition", {{"x", 1.5}, {"y", 2.5}}},
                        {"PrefabVelocity", {{"dx", 10.0}, {"dy", 0.0}}}}},
                      {"tags", {"bullet", "hostile"}}};

  Prefab prefab = PrefabLoader::loadFromJson(j);

  EXPECT_EQ(prefab.components.size(), 2u);
  const nlohmann::json *pos = findComponent(prefab, "PrefabPosition");
  ASSERT_NE(pos, nullptr);
  EXPECT_FLOAT_EQ((*pos)["x"].get<float>(), 1.5f);

  ASSERT_EQ(prefab.tags.size(), 2u);
  EXPECT_EQ(prefab.tags[0], "bullet");
  EXPECT_EQ(prefab.tags[1], "hostile");
}

// PrefabLoader handles a missing components or tags section without throwing.
TEST(PrefabLoader, MissingSectionsYieldEmpty) {
  nlohmann::json j = nlohmann::json::object();
  Prefab prefab = PrefabLoader::loadFromJson(j);
  EXPECT_TRUE(prefab.components.empty());
  EXPECT_TRUE(prefab.tags.empty());
}

// PrefabLoader::loadFromBytes parses a UTF-8 byte buffer as JSON.
TEST(PrefabLoader, LoadFromBytesParsesUtf8Buffer) {
  std::string text = R"({"components":{"PrefabPosition":{"x":7,"y":8}}})";
  std::vector<uint8_t> bytes(text.begin(), text.end());

  Prefab prefab = PrefabLoader::loadFromBytes(
      std::span<const uint8_t>(bytes.data(), bytes.size()));

  const nlohmann::json *pos = findComponent(prefab, "PrefabPosition");
  ASSERT_NE(pos, nullptr);
  EXPECT_FLOAT_EQ((*pos)["x"].get<float>(), 7.0f);
}

// World::instantiatePrefab creates an entity and applies each component via
// its registered jsonDeserialize.
TEST(World, InstantiatePrefabCreatesEntityWithComponents) {
  World world;
  registerPrefabPosition(world);
  registerPrefabVelocity(world);

  Prefab prefab;
  prefab.components.emplace_back("PrefabPosition",
                                 nlohmann::json{{"x", 3.0}, {"y", 4.0}});
  prefab.components.emplace_back("PrefabVelocity",
                                 nlohmann::json{{"dx", -1.0}, {"dy", 2.0}});

  EntityId id = world.instantiatePrefab(prefab);

  ASSERT_TRUE(world.isAlive(id));
  PrefabPosition *p = world.getComponent<PrefabPosition>(id);
  ASSERT_NE(p, nullptr);
  EXPECT_FLOAT_EQ(p->x, 3.0f);
  EXPECT_FLOAT_EQ(p->y, 4.0f);

  PrefabVelocity *v = world.getComponent<PrefabVelocity>(id);
  ASSERT_NE(v, nullptr);
  EXPECT_FLOAT_EQ(v->dx, -1.0f);
  EXPECT_FLOAT_EQ(v->dy, 2.0f);
}

// World::instantiatePrefab applies any tags listed on the prefab.
TEST(World, InstantiatePrefabAppliesTags) {
  World world;
  Prefab prefab;
  prefab.tags = {"enemy", "spawned"};

  EntityId id = world.instantiatePrefab(prefab);

  EXPECT_TRUE(world.hasTag(id, "enemy"));
  EXPECT_TRUE(world.hasTag(id, "spawned"));
}

// Overrides shallow-merge into the prefab's component data: matching keys are
// replaced; un-overridden keys retain the prefab default.
TEST(World, InstantiatePrefabRespectsOverrides) {
  World world;
  registerPrefabPosition(world);

  Prefab prefab;
  prefab.components.emplace_back("PrefabPosition",
                                 nlohmann::json{{"x", 1.0}, {"y", 2.0}});

  nlohmann::json overrides = {{"PrefabPosition", {{"x", 99.0}}}};

  EntityId id = world.instantiatePrefab(prefab, overrides);

  PrefabPosition *p = world.getComponent<PrefabPosition>(id);
  ASSERT_NE(p, nullptr);
  EXPECT_FLOAT_EQ(p->x, 99.0f);
  EXPECT_FLOAT_EQ(p->y, 2.0f);
}

// Component names not in the registry are skipped silently rather than
// throwing.
TEST(World, InstantiatePrefabSkipsUnknownComponents) {
  World world;
  registerPrefabPosition(world);

  Prefab prefab;
  prefab.components.emplace_back("PrefabPosition",
                                 nlohmann::json{{"x", 5.0}, {"y", 6.0}});
  prefab.components.emplace_back("DoesNotExist", nlohmann::json::object());

  EntityId id = world.instantiatePrefab(prefab);
  PrefabPosition *p = world.getComponent<PrefabPosition>(id);
  ASSERT_NE(p, nullptr);
  EXPECT_FLOAT_EQ(p->x, 5.0f);
}

// SceneLoader resolves "prefab": "<path>" by loading the prefab via the
// AssetManager and instantiating it. A scene-level "name" still becomes a tag.
TEST(SceneLoader, EntityWithPrefabReferenceInstantiates) {
  TempDir dir;

  nlohmann::json prefabJson = {
      {"components", {{"PrefabPosition", {{"x", 11.0}, {"y", 22.0}}}}},
      {"tags", {"bullet"}}};
  dir.writeFile("bullet.prefab.json", prefabJson.dump());

  nlohmann::json entities = nlohmann::json::array();
  entities.push_back({{"name", "shot1"}, {"prefab", "bullet.prefab.json"}});
  nlohmann::json sceneJson = {{"entities", entities}};
  dir.writeFile("level.scene.json", sceneJson.dump());

  World world;
  registerPrefabPosition(world);
  AssetManager mgr(dir.path());
  SceneLoader loader(world, mgr);
  loader.loadScene("level.scene.json");

  auto byName = world.findWithTag("shot1");
  ASSERT_EQ(byName.size(), 1u);
  EntityId id = *byName.begin();

  EXPECT_TRUE(world.hasTag(id, "bullet"));
  PrefabPosition *p = world.getComponent<PrefabPosition>(id);
  ASSERT_NE(p, nullptr);
  EXPECT_FLOAT_EQ(p->x, 11.0f);
  EXPECT_FLOAT_EQ(p->y, 22.0f);
}

// SceneLoader passes "overrides" through to instantiatePrefab.
TEST(SceneLoader, EntityWithPrefabOverridesAppliesThem) {
  TempDir dir;

  nlohmann::json prefabJson = {
      {"components", {{"PrefabPosition", {{"x", 0.0}, {"y", 0.0}}}}}};
  dir.writeFile("dot.prefab.json", prefabJson.dump());

  nlohmann::json entities = nlohmann::json::array();
  entities.push_back({{"prefab", "dot.prefab.json"},
                      {"overrides", {{"PrefabPosition", {{"x", 50.0}}}}}});
  nlohmann::json sceneJson = {{"entities", entities}};
  dir.writeFile("level.scene.json", sceneJson.dump());

  World world;
  registerPrefabPosition(world);
  AssetManager mgr(dir.path());
  SceneLoader loader(world, mgr);
  loader.loadScene("level.scene.json");

  // Single entity in the scene; pull it via view.
  int count = 0;
  EntityId found{};
  for (auto [id, pos] : world.view<PrefabPosition>()) {
    found = id;
    ++count;
    (void)pos;
  }
  ASSERT_EQ(count, 1);
  PrefabPosition *p = world.getComponent<PrefabPosition>(found);
  ASSERT_NE(p, nullptr);
  EXPECT_FLOAT_EQ(p->x, 50.0f);
  EXPECT_FLOAT_EQ(p->y, 0.0f);
}

// If a deserializer throws partway through, the partially-built entity is
// destroyed before the exception propagates: no entity, no tag, no leaked row.
TEST(World, InstantiatePrefabRollsBackOnDeserializerThrow) {
  World world;
  registerPrefabPosition(world);
  registerPrefabBoom(world);

  Prefab prefab;
  prefab.components.emplace_back("PrefabPosition",
                                 nlohmann::json{{"x", 1.0}, {"y", 2.0}});
  prefab.components.emplace_back("PrefabBoom", nlohmann::json::object());
  prefab.tags = {"shouldNotStick"};

  size_t entitiesBefore = 0;
  for (auto [id, pos] : world.view<PrefabPosition>()) {
    (void)id;
    (void)pos;
    ++entitiesBefore;
  }
  EXPECT_EQ(entitiesBefore, 0u);

  EXPECT_THROW(world.instantiatePrefab(prefab), std::runtime_error);

  size_t entitiesAfter = 0;
  for (auto [id, pos] : world.view<PrefabPosition>()) {
    (void)id;
    (void)pos;
    ++entitiesAfter;
  }
  EXPECT_EQ(entitiesAfter, 0u);
  EXPECT_TRUE(world.findWithTag("shouldNotStick").empty());
}

// Override with the wrong shape (scalar where the prefab default is an object)
// is almost always a typo. mergeShallow throws so the error surfaces
// immediately instead of silently resetting the component to defaults inside
// the deserializer.
TEST(World, InstantiatePrefabThrowsOnTypeMismatchedOverride) {
  World world;
  registerPrefabPosition(world);

  Prefab prefab;
  prefab.components.emplace_back("PrefabPosition",
                                 nlohmann::json{{"x", 1.0}, {"y", 2.0}});

  nlohmann::json bad = {{"PrefabPosition", 5}};
  EXPECT_THROW(world.instantiatePrefab(prefab, bad), std::runtime_error);

  // Atomic rollback applies here too — no leaked entity.
  size_t entitiesAfter = 0;
  for (auto [id, pos] : world.view<PrefabPosition>()) {
    (void)id;
    (void)pos;
    ++entitiesAfter;
  }
  EXPECT_EQ(entitiesAfter, 0u);
}

// Pin the spec'd semantics: overrides modify existing prefab components but do
// not introduce new ones. An override entry whose name isn't in the prefab is
// silently dropped. (If this ever flips intentionally, this test is the
// canary.)
TEST(World, InstantiatePrefabIgnoresOverridesForMissingComponents) {
  World world;
  registerPrefabPosition(world);
  registerPrefabVelocity(world);

  Prefab prefab;
  prefab.components.emplace_back("PrefabPosition",
                                 nlohmann::json{{"x", 1.0}, {"y", 2.0}});

  nlohmann::json overrides = {{"PrefabPosition", {{"x", 99.0}}},
                              {"PrefabVelocity", {{"dx", 7.0}, {"dy", 8.0}}}};

  EntityId id = world.instantiatePrefab(prefab, overrides);

  PrefabPosition *p = world.getComponent<PrefabPosition>(id);
  ASSERT_NE(p, nullptr);
  EXPECT_FLOAT_EQ(p->x, 99.0f);
  EXPECT_FALSE(world.hasComponent<PrefabVelocity>(id));
}

// Malformed prefab bytes propagate a parse error rather than being silently
// treated as empty. SceneLoader relies on this to abort a broken scene load.
TEST(PrefabLoader, LoadFromBytesThrowsOnMalformedJson) {
  std::string broken = R"({"components": {"PrefabPosition": )";
  std::vector<uint8_t> bytes(broken.begin(), broken.end());
  EXPECT_THROW(PrefabLoader::loadFromBytes(
                   std::span<const uint8_t>(bytes.data(), bytes.size())),
               nlohmann::json::parse_error);
}

// Shallow merge adds override keys that the prefab default doesn't declare,
// not just replaces matching ones. Pinned so the "only replace" misreading of
// mergeShallow doesn't sneak in later.
TEST(World, InstantiatePrefabOverrideAddsNewKeyToObject) {
  World world;
  registerPrefabPosition(world);

  Prefab prefab;
  prefab.components.emplace_back("PrefabPosition", nlohmann::json{{"x", 1.0}});

  nlohmann::json overrides = {{"PrefabPosition", {{"y", 99.0}}}};
  EntityId id = world.instantiatePrefab(prefab, overrides);

  PrefabPosition *p = world.getComponent<PrefabPosition>(id);
  ASSERT_NE(p, nullptr);
  EXPECT_FLOAT_EQ(p->x, 1.0f);
  EXPECT_FLOAT_EQ(p->y, 99.0f);
}

// When a scene entity carries both "prefab" and "components", SceneLoader
// takes the prefab path and drops the sibling components block. Pinned so the
// silent-drop is explicit — flip to a throw or a merge later if desired.
TEST(SceneLoader, EntityWithPrefabIgnoresSiblingComponentsBlock) {
  TempDir dir;

  nlohmann::json prefabJson = {
      {"components", {{"PrefabPosition", {{"x", 1.0}, {"y", 2.0}}}}}};
  dir.writeFile("dot.prefab.json", prefabJson.dump());

  nlohmann::json entities = nlohmann::json::array();
  entities.push_back(
      {{"prefab", "dot.prefab.json"},
       {"components", {{"PrefabVelocity", {{"dx", 9.0}, {"dy", 9.0}}}}}});
  nlohmann::json sceneJson = {{"entities", entities}};
  dir.writeFile("level.scene.json", sceneJson.dump());

  World world;
  registerPrefabPosition(world);
  registerPrefabVelocity(world);
  AssetManager mgr(dir.path());
  SceneLoader loader(world, mgr);
  loader.loadScene("level.scene.json");

  int count = 0;
  EntityId found{};
  for (auto [id, pos] : world.view<PrefabPosition>()) {
    found = id;
    ++count;
    (void)pos;
  }
  ASSERT_EQ(count, 1);
  EXPECT_FALSE(world.hasComponent<PrefabVelocity>(found));
}
