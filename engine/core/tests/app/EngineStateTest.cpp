#include <gtest/gtest.h>

#include "../assets/TempDir.hpp"
#include "Engine.hpp"

namespace {

const nlohmann::json& entityIn(const nlohmann::json& state, EntityId id) {
  for (const auto& e : state["entities"]) {
    if (e["id"] == nlohmann::json{id.index, id.generation}) return e;
  }
  static const nlohmann::json none;
  return none;
}

struct Health : Component<Health> {
  COMPONENT_NAME("Health");
  float hp = 3.0f;
  uint32_t team = 2;
};

}  // namespace

// The dump is the world as data: ids, tags (names), parents, and each
// component's script fields with the values scripts would read.
TEST(EngineState, DescribesEntitiesAndTheirScriptFields) {
  TempDir dir;
  Engine engine(dir.path(), "manifest.json");
  World& world = engine.getWorld();
  world.registerComponent<Health>({
      .scriptFields = {scriptField<Health>("hp", [](Health& c) -> float& { return c.hp; }),
                       scriptField<Health>("team", [](Health& c) -> uint32_t& { return c.team; })},
  });
  const EntityId hero = world.createEntity();
  world.addComponent<Health>(hero).hp = 7.5f;
  world.addTag(hero, "Hero");
  const EntityId sword = world.createEntity();
  world.setParent(sword, hero);

  const nlohmann::json state = engine.stateJson();
  EXPECT_EQ(state["frame"], 0);
  ASSERT_EQ(state["entities"].size(), 2u);
  const nlohmann::json& h = entityIn(state, hero);
  ASSERT_FALSE(h.is_null());
  EXPECT_EQ(h["tags"], (nlohmann::json{"Hero"}));
  EXPECT_EQ(h["components"]["Health"], (nlohmann::json{{"hp", 7.5}, {"team", 2}}));
  EXPECT_FALSE(h.contains("parent"));
  EXPECT_EQ(entityIn(state, sword)["parent"], (nlohmann::json{hero.index, hero.generation}));
  EXPECT_TRUE(state["session"].is_object());
}

TEST(EngineState, MarksEntitiesBeingDestroyed) {
  TempDir dir;
  Engine engine(dir.path(), "manifest.json");
  const EntityId gone = engine.getWorld().createEntity();
  engine.getWorld().destroyDeferred(gone);
  EXPECT_EQ(engine.stateJson()["entities"][0]["destroying"], true);
}
