#include <gtest/gtest.h>

#include <fstream>
#include <set>
#include <sstream>

#include "../assets/TempDir.hpp"
#include "Engine.hpp"
#include "PlaySession.hpp"

// A recording read back gives the run exactly: dts bit for bit, each event at
// its frame, focus changes, sample hashes and markers.
TEST(PlaySession, ARecordingReadsBackExactly) {
  TempDir dir;
  const auto path = dir.path() / "s";
  {
    session::Recorder recorder(path, {{"game", "Test"}, {"seed", 42}}, R"({"best": 3})");
    const nlohmann::json state = {{"time", 0.5}, {"scene", "scenes/a.scene.json"}, {"entities", {{{"id", {1, 0}}}}},
                                  {"session", {{"lives", 3}}}};
    recorder.input(0, {{"type", "key"}, {"name", "Space"}, {"down", true}});
    recorder.frameDone(0, 0.0166666675f, &state);
    recorder.input(1, {{"type", "move"}, {"x", 10.25f}, {"y", 3.1f}});
    recorder.input(1, {{"type", "focus"}, {"focused", false}});
    recorder.frameDone(1, 0.0213f, nullptr);
    EXPECT_EQ(recorder.marker(1, 0.04, state, "too fast"), 1);
    recorder.end("quit");
  }

  const session::Playback playback(path);
  EXPECT_EQ(playback.meta()["seed"], 42);
  EXPECT_EQ(playback.meta()["frames"], 2);
  EXPECT_EQ(playback.meta()["ended"], "quit");
  EXPECT_EQ(playback.meta()["markers"][0]["note"], "too fast");
  ASSERT_EQ(playback.frames(), 2u);
  EXPECT_EQ(playback.dt(0), 0.0166666675f);
  EXPECT_EQ(playback.dt(1), 0.0213f);
  ASSERT_EQ(playback.eventsAt(0).size(), 1u);
  EXPECT_EQ(playback.eventsAt(0)[0]["name"], "Space");
  ASSERT_EQ(playback.eventsAt(1).size(), 1u);  // focus is kept apart
  EXPECT_EQ(playback.eventsAt(1)[0].value("x", 0.0f), 10.25f);
  EXPECT_EQ(playback.eventsAt(1)[0].value("y", 0.0f), 3.1f);
  EXPECT_TRUE(playback.focusedAt(0));
  EXPECT_FALSE(playback.focusedAt(1));
  EXPECT_FALSE(playback.focusedAt(9));
  ASSERT_TRUE(playback.hashAt(0));
  EXPECT_FALSE(playback.hashAt(1));
  std::ifstream save(path / "save.json");
  std::string text((std::istreambuf_iterator<char>(save)), {});
  EXPECT_EQ(text, R"({"best": 3})");
}

// A crash leaves the session readable: everything up to the last flush, and
// a half-written last line is skipped.
TEST(PlaySession, ACrashedRecordingStillReads) {
  TempDir dir;
  const auto path = dir.path() / "s";
  {
    session::Recorder recorder(path, {{"seed", 1}}, "");
    for (uint64_t f = 0; f < 60; ++f) recorder.frameDone(f, 1.0f / 60.0f, nullptr);  // flushes at 60
    std::ofstream(path / "inputs.jsonl", std::ios::app) << R"({"f": 70, "type": "ke)";
    // no end(): the destructor would; read the files as a crash left them
    const session::Playback playback(path);
    EXPECT_EQ(playback.frames(), 60u);
    EXPECT_EQ(playback.meta()["ended"], "running");
    EXPECT_TRUE(playback.eventsAt(70).empty());
  }
}

TEST(PlaySession, NotASessionSaysSo) {
  TempDir dir;
  try {
    session::Playback playback(dir.path() / "nope");
    FAIL() << "expected an error";
  } catch (const std::runtime_error& e) {
    EXPECT_NE(std::string(e.what()).find("no session"), std::string::npos);
  }
}

TEST(PlaySession, TheHashFollowsTheEntitiesOnly) {
  const nlohmann::json a = {{"time", 1.0}, {"entities", {{{"id", {1, 0}}}}}};
  nlohmann::json b = a;
  b["time"] = 2.0;
  EXPECT_EQ(session::entitiesHash(a), session::entitiesHash(b));
  b["entities"][0]["id"] = {2, 0};
  EXPECT_NE(session::entitiesHash(a), session::entitiesHash(b));
}

// The timeline ends with the last frame: what happened after the last sample
// (a death in the final half second) is in it. One that ended on a sample
// isn't written twice.
TEST(PlaySession, TheTimelineEndsWithTheLastFrame) {
  TempDir dir;
  const nlohmann::json alive = {{"session", {{"deaths", 0}}}}, dead = {{"session", {{"deaths", 1}}}};
  auto lastLines = [](const std::filesystem::path& path) {
    std::ifstream in(path / "timeline.jsonl");
    std::vector<nlohmann::json> lines;
    for (std::string line; std::getline(in, line);) lines.push_back(nlohmann::json::parse(line));
    return lines;
  };
  {
    session::Recorder recorder(dir.path() / "a", {}, "");
    for (uint64_t f = 0; f < 45; ++f) recorder.frameDone(f, 1.0f / 60.0f, f % 30 == 0 ? &alive : nullptr);
    recorder.end("quit", &dead);
  }
  const auto a = lastLines(dir.path() / "a");
  ASSERT_EQ(a.size(), 3u);
  EXPECT_EQ(a.back()["f"], 44);
  EXPECT_EQ(a.back()["session"]["deaths"], 1);
  {
    session::Recorder recorder(dir.path() / "b", {}, "");
    for (uint64_t f = 0; f <= 30; ++f) recorder.frameDone(f, 1.0f / 60.0f, f % 30 == 0 ? &alive : nullptr);
    recorder.end("quit", &alive);
  }
  EXPECT_EQ(lastLines(dir.path() / "b").size(), 2u);
}

namespace {

// A one-scene game, driven for `commands`: recorded to `record`, or
// replaying `play`.
void driveGame(const TempDir& game, const std::string& commands, const std::filesystem::path& record,
               const std::filesystem::path& play = {}) {
  EngineOptions options;
  options.dev = DevOptions{};
  options.dev.drive = true;
  options.dev.recordDir = record;
  options.dev.playSession = play;
  if (play.empty()) options.dev.saveDir = game.path() / "save";
  Engine engine(game.path(), ".jm.json", options);
  engine.initialize();
  std::istringstream in(commands);
  std::ostringstream out;
  engine.drive(in, out);
}

std::set<std::filesystem::path> replaySaves() {
  std::set<std::filesystem::path> found;
  for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::temp_directory_path())) {
    if (entry.path().filename().string().rfind("jm-replay-", 0) == 0) found.insert(entry.path());
  }
  return found;
}

}  // namespace

// A replay never touches the player's save: it plays on a copy, deleted after.
TEST(PlaySession, AReplaysCopyOfTheSaveIsDeletedAfter) {
  TempDir game;
  game.writeFile(".jm.json", R"({"name": "Saved", "entryScene": "scenes/main.scene.json",
                                "scenes": ["scenes/main.scene.json"], "assets": []})");
  game.writeFile("scenes/main.scene.json", R"({"name": "main", "entities": []})");
  const auto play = game.path() / "play";
  driveGame(game, "step 5\n", play);
  const auto before = replaySaves();
  driveGame(game, "step 1\n", {}, play);
  EXPECT_EQ(replaySaves(), before);
}
