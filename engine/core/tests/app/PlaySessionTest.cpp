#include <gtest/gtest.h>

#include <fstream>

#include "../assets/TempDir.hpp"
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
