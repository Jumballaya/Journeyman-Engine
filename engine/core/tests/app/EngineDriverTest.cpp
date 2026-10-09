#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

#include "../assets/TempDir.hpp"
#include "Engine.hpp"

namespace {

// Runs the driver over `commands` on a one-scene project; the replies, parsed.
std::vector<nlohmann::json> drive(const std::string& commands) {
  TempDir dir;
  dir.writeFile(".jm.json", R"({"name": "Driven", "entryScene": "scenes/main.scene.json",
                               "scenes": ["scenes/main.scene.json"], "assets": []})");
  dir.writeFile("scenes/main.scene.json", R"({"name": "main", "entities": [{"name": "Hero", "components": {}}, {"name": "Rock", "components": {}}]})");
  EngineOptions options;
  options.dev = DevOptions{};
  options.dev.drive = true;
  options.dev.saveDir = dir.path() / "save";
  Engine engine(dir.path(), ".jm.json", options);
  engine.initialize();
  std::istringstream in(commands);
  std::ostringstream out;
  engine.drive(in, out);
  std::vector<nlohmann::json> replies;
  std::istringstream lines(out.str());
  for (std::string line; std::getline(lines, line);) replies.push_back(nlohmann::json::parse(line));
  return replies;
}

}  // namespace

TEST(EngineDriver, StepsOnlyWhenToldAndAnswersEachCommand) {
  const auto replies = drive("step 3\n\n# a comment\nstep\nstate\nquit\nstep 100\n");
  ASSERT_EQ(replies.size(), 5u);  // ready, step, step, state, quit: nothing after quit
  EXPECT_EQ(replies[0]["ready"], true);
  EXPECT_EQ(replies[0]["scene"], "scenes/main.scene.json");
  EXPECT_EQ(replies[1], (nlohmann::json{{"ok", true}, {"frame", 3}, {"errors", nlohmann::json::array()}}));
  EXPECT_EQ(replies[2]["frame"], 4);
  const nlohmann::json& state = replies[3]["state"];
  EXPECT_EQ(state["frame"], 3);  // the last frame run (4 have run: 0..3), as a dump of frame 3 says
  EXPECT_EQ(state["entities"].size(), 2u);
}

TEST(EngineDriver, SetsSessionValues) {
  const auto replies = drive("set lives 3\nset hero {\"name\": \"Ada\"}\nstate\n");
  EXPECT_EQ(replies[1]["ok"], true);
  EXPECT_EQ(replies[3]["state"]["session"], (nlohmann::json{{"hero", {{"name", "Ada"}}}, {"lives", 3}}));
}

// A bad command is answered and the run goes on.
TEST(EngineDriver, AnswersMistakesAndCarriesOn) {
  const auto replies = drive("jump\nstep many\nset lives\ndown Space\nstep 2\n");
  ASSERT_EQ(replies.size(), 6u);
  EXPECT_EQ(replies[1]["ok"], false);
  EXPECT_NE(replies[1]["error"].get<std::string>().find("unknown command 'jump'"), std::string::npos);
  EXPECT_EQ(replies[2]["ok"], false);
  EXPECT_EQ(replies[3]["ok"], false);
  EXPECT_EQ(replies[4]["ok"], false);  // no inputs module here to take keys
  EXPECT_EQ(replies[5]["frame"], 2);
}

TEST(EngineDriver, StatePicksPartsAndTaggedEntities) {
  const auto replies = drive("set lives 3\nstate session\nstate tag=Rock\nstate session tag=Hero\nstate sesion\nquit\n");
  ASSERT_EQ(replies.size(), 7u);
  EXPECT_EQ(replies[2]["state"], (nlohmann::json{{"frame", 0}, {"session", {{"lives", 3}}}}));
  const nlohmann::json& rocks = replies[3]["state"];
  EXPECT_EQ(rocks.size(), 2u);  // frame, entities
  ASSERT_EQ(rocks["entities"].size(), 1u);
  EXPECT_EQ(rocks["entities"][0]["tags"], (nlohmann::json{"Rock"}));
  EXPECT_EQ(replies[4]["state"]["entities"].size(), 1u);
  EXPECT_TRUE(replies[4]["state"].contains("session"));
  EXPECT_EQ(replies[5]["ok"], false);  // a typo is an error, not the whole state
  EXPECT_NE(replies[5]["error"].get<std::string>().find("session"), std::string::npos);
}

TEST(EngineDriver, StateFiltersByComponentAndGetAnswersOneValue) {
  // Core alone registers no components the scene uses, so the Hero has none:
  // a component filter drops it, and a path into it fails.
  const auto replies = drive(
      "set lives 3\nstate tag=Hero TransformComponent\nget session.lives\nget tag=Hero TransformComponent.x\n"
      "get scene\nget\nquit\n");
  ASSERT_EQ(replies.size(), 8u);  // ready, set, state, 4 gets, quit
  EXPECT_EQ(replies[2]["state"]["entities"], nlohmann::json::array());
  EXPECT_EQ(replies[3], (nlohmann::json{{"ok", true}, {"value", 3}}));
  EXPECT_EQ(replies[4]["ok"], false);
  EXPECT_EQ(replies[5]["value"], "scenes/main.scene.json");
  EXPECT_EQ(replies[6]["ok"], false);
}

TEST(EngineDriver, AHugeIndexIsAnErrorNotACrash) {
  const auto replies = drive("get session.99999999999999999999999\nget entities.99999999999999999999999\nquit\n");
  ASSERT_EQ(replies.size(), 4u);  // ready, 2 gets, quit: the run went on
  EXPECT_EQ(replies[1]["ok"], false);
  EXPECT_EQ(replies[2]["ok"], false);
}
