#include <gtest/gtest.h>

#include <cstddef>
#include <nlohmann/json.hpp>
#include <span>
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
  world.registerComponent<SceneTestPosition, SceneTestPosition>(
      [](World& w, EntityId id, const nlohmann::json& j) {
        SceneTestPosition p;
        if (j.contains("x")) p.x = j["x"].get<float>();
        if (j.contains("y")) p.y = j["y"].get<float>();
        w.addComponent<SceneTestPosition>(id, p);
      },
      [](const World&, EntityId, nlohmann::json&) { return false; },
      [](World&, EntityId, std::span<const std::byte>) { return false; },
      [](const World&, EntityId, std::span<std::byte>, size_t&) { return false; });
}

void writeScene(const TempDir& dir, const std::string& rel, const nlohmann::json& j) {
  dir.writeFile(rel, j.dump());
}

}  // namespace

// The JSON "name" field at the top level becomes the current scene name.
TEST(SceneLoader, LoadScenePopulatesCurrentSceneName) {
  TempDir dir;
  writeScene(dir, "level.scene.json",
             {{"name", "level1"}, {"entities", nlohmann::json::array()}});

  World world;
  AssetManager mgr(dir.path());
  SceneLoader loader(world, mgr);
  loader.loadScene("level.scene.json");

  EXPECT_EQ(loader.getCurrentSceneName(), "level1");
}

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

// An entity's "name" field is applied to the entity as a tag.
TEST(SceneLoader, EntityNameFieldBecomesTag) {
  TempDir dir;
  nlohmann::json entities = nlohmann::json::array();
  entities.push_back({{"name", "Player"}});
  writeScene(dir, "level.scene.json", {{"entities", entities}});

  World world;
  AssetManager mgr(dir.path());
  SceneLoader loader(world, mgr);
  loader.loadScene("level.scene.json");

  auto tagged = world.findWithTag("Player");
  EXPECT_EQ(tagged.size(), 1u);
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
