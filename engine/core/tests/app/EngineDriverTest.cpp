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
  dir.writeFile("scenes/main.scene.json", R"({"name": "main", "entities": [{"name": "Hero", "components": {}}]})");
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
  EXPECT_EQ(state["frame"], 4);
  ASSERT_EQ(state["entities"].size(), 1u);
  EXPECT_EQ(state["entities"][0]["tags"], (nlohmann::json{"Hero"}));
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
