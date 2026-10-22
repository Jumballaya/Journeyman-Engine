#include <gtest/gtest.h>

#include <fstream>

#include "../assets/TempDir.hpp"
#include "Engine.hpp"
#include "LoggerService.hpp"

namespace {

// JM_INSPECT over a game whose entry scene is `scene` (and its files).
nlohmann::json inspect(const std::string& scene, const std::function<void(TempDir&)>& files = {}) {
  TempDir dir;
  dir.writeFile(".jm.json", R"({"name": "Seen", "entryScene": "scenes/main.scene.json", "scenes": ["scenes/main.scene.json"]})");
  dir.writeFile("scenes/main.scene.json", scene);
  if (files) files(dir);
  EngineOptions options;
  options.dev = DevOptions{};
  options.dev.inspectOut = dir.path() / "seen.json";
  options.dev.saveDir = dir.path() / "save";
  std::vector<std::string> errors;
  LoggerService::instance().setErrorListener(
      [&](LogLevel, std::string_view message, const ErrorSource&) { errors.emplace_back(message); });
  {
    Engine engine(dir.path(), ".jm.json", options);
    engine.initialize();
    engine.run();
  }
  LoggerService::instance().setErrorListener(nullptr);
  std::ifstream in(options.dev.inspectOut);
  nlohmann::json seen = nlohmann::json::parse(in);
  seen["logged"] = errors;  // the test's: what the run reported
  return seen;
}

}  // namespace

// Each entity as the engine builds it: where its entry is, its parent, the
// JSON its components were made from (a prefab's with the entry's overrides);
// no script loads (none is there to), nothing runs.
TEST(Inspect, TheSceneAsTheEngineBuildsIt) {
  const auto seen = inspect(R"({"name": "main", "entities": [
      {"name": "Hero", "prefab": "prefabs/hero.prefab.json",
       "overrides": {"ScriptComponent": {"params": {"speed": 3}}}},
      {"name": "Wave", "group": "wave1", "components": {}},
      {"name": "Key", "if": "door.open", "components": {}}]})",
                            [](TempDir& dir) {
                              dir.writeFile("prefabs/hero.prefab.json", R"({"components": {
                                "ScriptComponent": {"script": "scripts/missing.ts", "params": {"speed": 1, "jump": 2}}},
                                "children": [{"name": "Shadow", "components": {}}]})");
                            });
  EXPECT_FALSE(seen.contains("error"));
  EXPECT_EQ(seen["logged"], nlohmann::json::array());  // its script wasn't loaded
  EXPECT_EQ(seen["scene"], "scenes/main.scene.json");
  ASSERT_EQ(seen["entities"].size(), 2u);
  const auto& hero = seen["entities"][0];
  EXPECT_EQ(hero["name"], "Hero");
  EXPECT_EQ(hero["origin"]["entry"], "0");
  EXPECT_EQ(hero["origin"]["prefab"], "prefabs/hero.prefab.json");
  EXPECT_TRUE(hero["parent"].is_null());
  EXPECT_EQ(hero["components"]["ScriptComponent"]["params"], (nlohmann::json{{"speed", 3}, {"jump", 2}}));
  const auto& shadow = seen["entities"][1];
  EXPECT_EQ(shadow["name"], "Shadow");
  EXPECT_EQ(shadow["origin"]["entry"], "0/p0");
  EXPECT_EQ(shadow["parent"], hero["id"]);
  EXPECT_EQ(seen["skipped"], (nlohmann::json{{{"entry", "1"}, {"reason", "group wave1"}}, {{"entry", "2"}, {"reason", "if door.open"}}}));
}

// A scene the engine can't build is no entities, and why.
TEST(Inspect, ASceneThatFailsIsItsError) {
  const auto seen = inspect(R"({"name": "main", "entities": [{"name": "A", "components": {}},
      {"name": "Bad", "prefab": "prefabs/bad.prefab.json"}]})",
                            [](TempDir& dir) { dir.writeFile("prefabs/bad.prefab.json", "{not json"); });
  EXPECT_TRUE(seen["entities"].empty());
  EXPECT_TRUE(seen["error"].is_string() && !seen["error"].get<std::string>().empty());
}
