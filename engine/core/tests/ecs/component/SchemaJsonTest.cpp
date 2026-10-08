#include <gtest/gtest.h>

#include "World.hpp"
#include "component/Component.hpp"
#include "component/SchemaJson.hpp"

namespace {

struct Health : Component<Health> {
  COMPONENT_NAME("Health");
  float hp = 3.0f;
  uint32_t team = 0;
};

struct Marker : Component<Marker> {
  COMPONENT_NAME("Marker");
};

}  // namespace

TEST(SchemaJson, DescribesFieldsAndScriptFields) {
  World world;
  world.registerComponent<Health>({
      .scriptFields = {scriptField<Health>("hp", [](Health& c) -> float& { return c.hp; }),
                       scriptField<Health>("team", [](Health& c) -> uint32_t& { return c.team; })},
      .schema = {"Health", "Gameplay", "Hit points",
                 {FieldSchema::number("hp", 3, "Starting hit points", 0, 100, 1),
                  FieldSchema::choice("mood", {"calm", "angry"}),
                  FieldSchema::group("shield", {FieldSchema::boolean("on", true)})}},
  });
  world.registerComponent<Marker>();

  const nlohmann::json out = schemaJson(world.getComponentRegistry());
  EXPECT_EQ(out["schemaVersion"], 1);
  const nlohmann::json& health = out["components"]["Health"];
  EXPECT_EQ(health["category"], "Gameplay");
  EXPECT_EQ(health["fields"][0], (nlohmann::json{{"key", "hp"}, {"kind", "number"}, {"default", 3.0}, {"hint", "Starting hit points"},
                                                 {"min", 0.0}, {"max", 100.0}, {"step", 1.0}}));
  EXPECT_EQ(health["fields"][1]["choices"], (nlohmann::json{"calm", "angry"}));
  EXPECT_EQ(health["fields"][1]["default"], "calm");
  EXPECT_EQ(health["fields"][2]["fields"][0]["kind"], "bool");
  EXPECT_EQ(health["scriptFields"], (nlohmann::json{{{"name", "hp"}, {"type", "f32"}}, {{"name", "team"}, {"type", "u32"}}}));
  // A component with no schema is still listed: scenes may name it.
  EXPECT_TRUE(out["components"]["Marker"]["fields"].empty());
  EXPECT_FALSE(out.contains("hostFunctions"));
}

TEST(SchemaJson, ListsHostFunctionSignatures) {
  World world;
  const nlohmann::json out = schemaJson(world.getComponentRegistry(), {{"__jmLog", "v(ii)"}, {"__jmSelf", "I()"}});
  EXPECT_EQ(out["hostFunctions"], (nlohmann::json{{"__jmLog", "v(ii)"}, {"__jmSelf", "I()"}}));
}
