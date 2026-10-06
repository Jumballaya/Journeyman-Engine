#include <gtest/gtest.h>

#include <cstddef>
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <string>

#include "../assets/TempDir.hpp"
#include "AssetManager.hpp"
#include "SceneLoader.hpp"
#include "World.hpp"
#include "component/Component.hpp"

namespace {

struct SceneTestPosition : Component<SceneTestPosition> {
  COMPONENT_NAME("SceneTestPosition");
  float x = 0.0f;
  float y = 0.0f;
};

void registerSceneTestPosition(World& world) {
  world.registerComponent<SceneTestPosition>(
      {.fromJson = [](SceneTestPosition& p, const nlohmann::json& j, EntityId) {
        if (j.contains("x")) p.x = j["x"].get<float>();
        if (j.contains("y")) p.y = j["y"].get<float>();
      }});
}

void writeScene(const TempDir& dir, const std::string& rel, const nlohmann::json& j) {
  dir.writeFile(rel, j.dump());
}

}  // namespace

// Each element in the "entities" array produces a new entity in the world.
TEST(SceneLoader, LoadSceneCreatesEntitiesFromArray) {
  TempDir dir;
  nlohmann::json entities = nlohmann::json::array();
  entities.push_back({{"name", "a"}});
  entities.push_back({{"name", "b"}});
  entities.push_back({{"name", "c"}});
  writeScene(dir, "level.scene.json", {{"entities", entities}});

  World world;
  AssetManager mgr(dir.path());
  SceneLoader loader(world, mgr);
  loader.loadScene("level.scene.json");

  EXPECT_EQ(world.findWithTag("a").size(), 1u);
  EXPECT_EQ(world.findWithTag("b").size(), 1u);
  EXPECT_EQ(world.findWithTag("c").size(), 1u);
}

// A component listed under an entity's "components" object triggers its
// registered jsonDeserialize with the JSON body.
TEST(SceneLoader, ComponentJsonDeserializerInvokedWithData) {
  TempDir dir;
  nlohmann::json components;
  components["SceneTestPosition"] = {{"x", 3.5}, {"y", -2.5}};
  nlohmann::json entities = nlohmann::json::array();
  entities.push_back({{"name", "obj"}, {"components", components}});
  writeScene(dir, "level.scene.json", {{"entities", entities}});

  World world;
  registerSceneTestPosition(world);
  AssetManager mgr(dir.path());
  SceneLoader loader(world, mgr);
  loader.loadScene("level.scene.json");

  auto tagged = world.findWithTag("obj");
  ASSERT_EQ(tagged.size(), 1u);
  EntityId id = *tagged.begin();
  SceneTestPosition* p = world.getComponent<SceneTestPosition>(id);
  ASSERT_NE(p, nullptr);
  EXPECT_FLOAT_EQ(p->x, 3.5f);
  EXPECT_FLOAT_EQ(p->y, -2.5f);
}

// A component name the registry does not know is silently skipped — the load
// does not throw, and known components on the same entity still deserialize.
TEST(SceneLoader, UnknownComponentIsSilentlySkipped) {
  TempDir dir;
  nlohmann::json components;
  components["NotARealComponent"] = nlohmann::json::object();
  components["SceneTestPosition"] = {{"x", 1.0}, {"y", 2.0}};
  nlohmann::json entities = nlohmann::json::array();
  entities.push_back({{"name", "obj"}, {"components", components}});
  writeScene(dir, "level.scene.json", {{"entities", entities}});

  World world;
  registerSceneTestPosition(world);
  AssetManager mgr(dir.path());
  SceneLoader loader(world, mgr);

  EXPECT_NO_THROW(loader.loadScene("level.scene.json"));

  auto tagged = world.findWithTag("obj");
  ASSERT_EQ(tagged.size(), 1u);
  EntityId id = *tagged.begin();
  EXPECT_NE(world.getComponent<SceneTestPosition>(id), nullptr);
}

// A failing entry leaves nothing behind (group spawns rely on this).
TEST(SceneLoader, FailedEntryLeavesNoEntity) {
  TempDir dir;
  World world;
  world.registerComponent<SceneTestPosition>(
      {.fromJson = [](SceneTestPosition&, const nlohmann::json&, EntityId) { throw std::runtime_error("bad"); }});
  AssetManager mgr(dir.path());
  SceneLoader loader(world, mgr);

  const nlohmann::json entry = {{"name", "obj"}, {"components", {{"SceneTestPosition", nlohmann::json::object()}}}};
  EXPECT_THROW(loader.createEntityFromJson(entry), std::runtime_error);
  EXPECT_TRUE(world.findWithTag("obj").empty());
}

// Entries nest: "children" attach to their entry, prefabs bring theirs, and an
// instance's overrides reach its prefab's children by name.
TEST(SceneLoader, ChildrenAttachToTheirEntryAndPrefab) {
  TempDir dir;
  dir.writeFile("bat.prefab.json", nlohmann::json{
      {"components", {{"SceneTestPosition", {{"x", 1}}}}},
      {"children", {{{"name", "wing"}, {"components", {{"SceneTestPosition", {{"x", 2}}}}}}}}}.dump());
  nlohmann::json entities = nlohmann::json::array();
  entities.push_back({{"name", "house"},
                      {"components", {{"SceneTestPosition", {{"x", 0}}}}},
                      {"children", {{{"name", "door"}, {"children", {{{"name", "knob"}}}}}}}});
  entities.push_back({{"name", "bat"}, {"prefab", "bat.prefab.json"},
                      {"overrides", {{"children", {{"wing", {{"SceneTestPosition", {{"y", 5}}}}}}}}}});
  writeScene(dir, "level.scene.json", {{"entities", entities}});

  World world;
  registerSceneTestPosition(world);
  AssetManager mgr(dir.path());
  SceneLoader loader(world, mgr);
  const auto roots = loader.loadScene("level.scene.json");
  ASSERT_EQ(roots.size(), 2u);  // children aren't roots
  const EntityId house = *world.findWithTag("house").begin(), door = *world.findWithTag("door").begin();
  EXPECT_EQ(world.parentOf(door), house);
  EXPECT_EQ(world.parentOf(*world.findWithTag("knob").begin()), door);
  const EntityId wing = *world.findWithTag("wing").begin();
  EXPECT_EQ(world.parentOf(wing), *world.findWithTag("bat").begin());
  EXPECT_FLOAT_EQ(world.getComponent<SceneTestPosition>(wing)->x, 2.0f);
  EXPECT_FLOAT_EQ(world.getComponent<SceneTestPosition>(wing)->y, 5.0f);
  world.destroyEntity(house);
  EXPECT_TRUE(world.findWithTag("knob").empty());
}
