#include <gtest/gtest.h>

#include <fstream>
#include <map>
#include <functional>
#include <cstdlib>
#include <sstream>

#include "../assets/TempDir.hpp"
#include "AgentRun.hpp"
#include "Engine.hpp"
#include "PlaySession.hpp"
#include "WindowEvents.hpp"

// A recording read back gives the run exactly: dts bit for bit, each event at
// its frame, focus changes, sample hashes and markers.
TEST(PlaySession, ARecordingReadsBackExactly) {
  TempDir dir;
  const auto path = dir.path() / "s";
  const nlohmann::json state = {{"time", 0.5}, {"scene", "scenes/a.scene.json"}, {"entities", {{{"id", {1, 0}}}}},
                                {"session", {{"lives", 3}}}};
  {
    session::Recorder recorder(path, {{"game", "Test"}, {"seed", 42}}, R"({"best": 3})");
    recorder.frameStarts(0, true);
    recorder.input({{"type", "key"}, {"name", "Space"}, {"down", true}});
    const auto thumb = recorder.frameDone(0, 0.0166666675f, [&]() -> const nlohmann::json& { return state; });
    EXPECT_EQ(thumb, std::optional(path / "thumbs" / "000000.jpg"));
    recorder.frameStarts(1, false);
    recorder.input({{"type", "move"}, {"x", 10.25f}, {"y", 3.1f}});
    EXPECT_FALSE(recorder.frameDone(1, 0.0213f, nullptr));  // not sampled: state isn't asked for
    recorder.input({{"type", "wheel"}, {"dx", 0.0f}, {"dy", 1.0f}});  // between frames: before the next
    const auto marker = recorder.marker(1, 0.04, state, "too fast");
    EXPECT_EQ(marker.n, 1);
    EXPECT_EQ(marker.image, path / "markers" / "1.png");
    recorder.end();
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
  ASSERT_EQ(playback.eventsBefore(2).size(), 1u);
  EXPECT_EQ(playback.eventsBefore(2)[0]["type"], "wheel");
  EXPECT_TRUE(playback.focusedAt(0));
  EXPECT_FALSE(playback.focusedAt(1));
  EXPECT_FALSE(playback.focusedAt(9));
  const session::LazyState same = [&]() -> const nlohmann::json& { return state; };
  EXPECT_EQ(playback.matchesAt(0, same), std::optional(true));
  EXPECT_FALSE(playback.matchesAt(1, same));
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
    const nlohmann::json empty = nlohmann::json::object();
    const session::LazyState state = [&]() -> const nlohmann::json& { return empty; };
    for (uint64_t f = 0; f < 60; ++f) recorder.frameDone(f, 1.0f / 60.0f, state);  // flushes at 60
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
  const session::LazyState aliveState = [&]() -> const nlohmann::json& { return alive; };
  auto lastLines = [](const std::filesystem::path& path) {
    std::ifstream in(path / "timeline.jsonl");
    std::vector<nlohmann::json> lines;
    for (std::string line; std::getline(in, line);) lines.push_back(nlohmann::json::parse(line));
    return lines;
  };
  {
    session::Recorder recorder(dir.path() / "a", {}, "");
    for (uint64_t f = 0; f < 45; ++f) recorder.frameDone(f, 1.0f / 60.0f, aliveState);
    recorder.end(&dead);
  }
  const auto a = lastLines(dir.path() / "a");
  ASSERT_EQ(a.size(), 3u);
  EXPECT_EQ(a.back()["f"], 44);
  EXPECT_EQ(a.back()["session"]["deaths"], 1);
  {
    session::Recorder recorder(dir.path() / "b", {}, "");
    for (uint64_t f = 0; f <= 30; ++f) recorder.frameDone(f, 1.0f / 60.0f, aliveState);
    recorder.end(&alive);
  }
  EXPECT_EQ(lastLines(dir.path() / "b").size(), 2u);
}

namespace {

// A one-scene game, driven for `commands`: recorded to `record`, or
// replaying `play`, with the renderer's size given as `framebuffer`.
// Returns the driver's replies.
std::string driveGame(const TempDir& game, const std::string& commands, Engine::ViewSize framebuffer,
                      const std::filesystem::path& record, const std::filesystem::path& play = {},
                      const std::function<void(Engine&)>& setup = {},
                      const std::function<void(DevOptions&)>& configure = {}) {
  EngineOptions options;
  options.dev = DevOptions{};
  options.dev.drive = true;
  options.dev.recordDir = record;
  options.dev.playSession = play;
  if (configure) configure(options.dev);
  if (play.empty()) options.dev.saveDir = game.path() / "save";
  Engine engine(game.path(), ".jm.json", options);
  engine.setFramebufferSize(framebuffer.width, framebuffer.height);  // as the renderer does, starting
  engine.initialize();
  if (setup) setup(engine);
  std::istringstream in(commands);
  std::ostringstream out;
  engine.agent().drive(in, out);
  return out.str();
}

// The driver's last reply.
nlohmann::json lastReply(const std::string& replies) {
  const size_t end = replies.find_last_not_of('\n');
  return nlohmann::json::parse(replies.substr(replies.rfind('\n', end) + 1, std::string::npos));
}

// The OS temp dir (where a replay copies the save) is `dir` while this lives:
// other tests running at once can't put theirs there.
class TempDirIs {
 public:
  explicit TempDirIs(const std::filesystem::path& dir) : _was(get()) { set(dir.string()); }
  ~TempDirIs() { set(_was); }

 private:
#ifdef _WIN32
  static constexpr const char* kVar = "TMP";
#else
  static constexpr const char* kVar = "TMPDIR";
#endif
  std::string _was;
  static std::string get() {
    const char* v = std::getenv(kVar);
    return v ? v : "";
  }
  static void set(const std::string& value) {
#ifdef _WIN32
    _putenv_s(kVar, value.c_str());
#else
    if (value.empty()) unsetenv(kVar);
    else setenv(kVar, value.c_str(), 1);
#endif
  }
};

}  // namespace

namespace {

// What size the game draws at each frame, as its systems see it.
struct SizeProbe : System {
  Engine& engine;
  std::shared_ptr<std::vector<Engine::ViewSize>> seen;
  SizeProbe(Engine& e, std::shared_ptr<std::vector<Engine::ViewSize>> s) : engine(e), seen(std::move(s)) {}
  void update(World&, float) override { seen->push_back(engine.framebufferSize()); }
};

}  // namespace

// Pointer positions are framebuffer pixels: a play made on a 2x screen
// replays at its size on a 1x one from its first frame, so the same pixels
// land on the same spots.
TEST(PlaySession, AReplayStartsAtTheRecordedFramebufferSize) {
  TempDir game;
  game.writeFile(".jm.json", R"({"name": "Sized", "entryScene": "scenes/main.scene.json",
                                "scenes": ["scenes/main.scene.json"], "assets": []})");
  game.writeFile("scenes/main.scene.json", R"({"name": "main", "entities": []})");
  const auto play = game.path() / "play";
  driveGame(game, "step 5\n", {2560, 1440}, play);
  std::ifstream in(play / "session.json");
  const auto meta = nlohmann::json::parse(in);
  EXPECT_EQ(meta["framebuffer"], (nlohmann::json{2560, 1440}));

  auto seen = std::make_shared<std::vector<Engine::ViewSize>>();
  driveGame(game, "step 1\n", {1280, 720}, {}, play,
            [&](Engine& e) { e.getWorld().registerSystem<SizeProbe>(e, seen); });
  ASSERT_EQ(seen->size(), 1u);
  EXPECT_EQ((*seen)[0].width, 2560);
  EXPECT_EQ((*seen)[0].height, 1440);
}

// A replay never touches the player's save: it plays on a copy, deleted after.
TEST(PlaySession, AReplaysCopyOfTheSaveIsDeletedAfter) {
  TempDir game;
  game.writeFile(".jm.json", R"({"name": "Saved", "entryScene": "scenes/main.scene.json",
                                "scenes": ["scenes/main.scene.json"], "assets": []})");
  game.writeFile("scenes/main.scene.json", R"({"name": "main", "entities": []})");
  const auto play = game.path() / "play";
  driveGame(game, "step 5\n", {640, 360}, play);
  TempDir temp;
  const TempDirIs scoped(temp.path());
  bool copied = false;
  driveGame(game, "step 1\n", {640, 360}, {}, play,
            [&](Engine&) { copied = !std::filesystem::is_empty(temp.path()); });
  EXPECT_TRUE(copied);
  EXPECT_TRUE(std::filesystem::is_empty(temp.path()));
}

namespace {

// Notes whether the window has focus each frame, as a script would read it;
// at loseFocusAt it then loses focus, as the window module reports it (after
// the systems).
struct FocusProbe : System {
  Engine& engine;
  std::shared_ptr<std::vector<bool>> seen;
  std::optional<size_t> loseFocusAt;
  FocusProbe(Engine& e, std::shared_ptr<std::vector<bool>> s, std::optional<size_t> at)
      : engine(e), seen(std::move(s)), loseFocusAt(at) {}
  void update(World&, float) override {
    seen->push_back(engine.windowFocused());
    if (loseFocusAt == seen->size() - 1) engine.setWindowFocused(false);
  }
};

}  // namespace

// The player switching away mid-frame: the game sees it from the next frame,
// and so does the replay, not a frame early.
TEST(PlaySession, AReplaySeesFocusChangeOnTheFrameThePlayerDid) {
  TempDir game;
  game.writeFile(".jm.json", R"({"name": "Focus", "entryScene": "scenes/main.scene.json",
                                "scenes": ["scenes/main.scene.json"], "assets": []})");
  game.writeFile("scenes/main.scene.json", R"({"name": "main", "entities": []})");
  const auto play = game.path() / "play";
  auto played = std::make_shared<std::vector<bool>>();
  auto replayed = std::make_shared<std::vector<bool>>();
  driveGame(game, "step 10\n", {640, 360}, play, {},
            [&](Engine& e) { e.getWorld().registerSystem<FocusProbe>(e, played, size_t{5}); });
  driveGame(game, "step 10\n", {640, 360}, {}, play,
            [&](Engine& e) { e.getWorld().registerSystem<FocusProbe>(e, replayed, std::nullopt); });
  ASSERT_EQ(played->size(), 10u);
  EXPECT_TRUE((*played)[5]);
  EXPECT_FALSE((*played)[6]);
  EXPECT_EQ(*replayed, *played);
}

// A replay recorded again (jm plays resume) keeps the focus it replayed, not
// this machine's window's.
TEST(PlaySession, ARerecordedReplayKeepsItsFocus) {
  TempDir game;
  game.writeFile(".jm.json", R"({"name": "Focus", "entryScene": "scenes/main.scene.json",
                                "scenes": ["scenes/main.scene.json"], "assets": []})");
  game.writeFile("scenes/main.scene.json", R"({"name": "main", "entities": []})");
  const auto play = game.path() / "play", again = game.path() / "again";
  auto played = std::make_shared<std::vector<bool>>();
  auto replayed = std::make_shared<std::vector<bool>>();
  driveGame(game, "step 10\n", {640, 360}, play, {},
            [&](Engine& e) { e.getWorld().registerSystem<FocusProbe>(e, played, size_t{5}); });
  driveGame(game, "step 10\n", {640, 360}, again, play);
  driveGame(game, "step 10\n", {640, 360}, {}, again,
            [&](Engine& e) { e.getWorld().registerSystem<FocusProbe>(e, replayed, std::nullopt); });
  EXPECT_EQ(*replayed, *played);
}

// A long hitch runs at most kMaxDeltaTime of steps (the game slows rather than
// piling up steps), each recorded as the fixed step it was.
TEST(PlaySession, AHitchSlowsTheGameRatherThanPilingUpSteps) {
  TempDir game;
  game.writeFile(".jm.json", R"({"name": "Hitch", "entryScene": "scenes/main.scene.json",
                                "scenes": ["scenes/main.scene.json"], "assets": []})");
  game.writeFile("scenes/main.scene.json", R"({"name": "main", "entities": []})");
  const auto play = game.path() / "play";
  {
    EngineOptions options;
    options.dev = DevOptions{};
    options.dev.recordDir = play;
    options.dev.saveDir = game.path() / "save";
    Engine engine(game.path(), ".jm.json", options);
    engine.initialize();
    engine.frame(2.5f);
    EXPECT_EQ(engine.frameCount(), 6u);
  }
  const session::Playback playback(play);
  ASSERT_EQ(playback.frames(), 6u);
  EXPECT_EQ(playback.dt(5), 1.0f / 60.0f);
  EXPECT_NEAR(playback.meta()["seconds"].get<double>(), 0.1, 1e-6);
}

// Plays go under .jm/plays, which ignores itself, each in a folder of its own.
TEST(PlaySession, ANewPlayGetsAnUnusedFolder) {
  TempDir project;
  const auto first = session::newPlayDir(project.path());
  EXPECT_EQ(first.parent_path(), project.path() / ".jm" / "plays");
  std::ifstream ignore(project.path() / ".jm" / ".gitignore");
  std::string text((std::istreambuf_iterator<char>(ignore)), {});
  EXPECT_EQ(text, "*\n");
  EXPECT_TRUE(std::filesystem::is_directory(first));  // taken: the next one started this second can't have it
  const auto second = session::newPlayDir(project.path());
  EXPECT_NE(second, first);
  EXPECT_TRUE(std::filesystem::is_empty(second));
}

namespace {

// A session value each frame, as a script would read it.
struct SessionProbe : System {
  Engine& engine;
  std::shared_ptr<std::vector<nlohmann::json>> seen;
  SessionProbe(Engine& e, std::shared_ptr<std::vector<nlohmann::json>> s) : engine(e), seen(std::move(s)) {}
  void update(World&, float) override { seen->push_back(engine.getSession().getJson("score").value_or(nullptr)); }
};

// Whether a replayed key has arrived, at each frame's systems.
struct KeyProbe : System {
  std::shared_ptr<bool> arrived;
  std::shared_ptr<std::vector<bool>> seen;
  KeyProbe(std::shared_ptr<bool> a, std::shared_ptr<std::vector<bool>> s) : arrived(std::move(a)), seen(std::move(s)) {}
  void update(World&, float) override { seen->push_back(*arrived); }
};

void writeOneSceneGame(TempDir& game) {
  game.writeFile(".jm.json", R"({"name": "Driven", "entryScene": "scenes/main.scene.json",
                                "scenes": ["scenes/main.scene.json"], "assets": []})");
  game.writeFile("scenes/main.scene.json", R"({"name": "main", "entities": []})");
}

}  // namespace

// The driver's `set`, before the first frame and between later ones, is in the
// play: its replay's systems see each value from the frame the player's did.
TEST(PlaySession, ADriversSetReplaysOnTheFrameItWasGiven) {
  TempDir game;
  writeOneSceneGame(game);
  const auto play = game.path() / "play";
  auto played = std::make_shared<std::vector<nlohmann::json>>();
  auto replayed = std::make_shared<std::vector<nlohmann::json>>();
  driveGame(game, "set score 5\nstep 3\nset score 7\nstep 3\n", {640, 360}, play, {},
            [&](Engine& e) { e.getWorld().registerSystem<SessionProbe>(e, played); });
  const std::string replies = driveGame(game, "step 6\nstate replay\n", {640, 360}, {}, play,
                                        [&](Engine& e) { e.getWorld().registerSystem<SessionProbe>(e, replayed); });
  EXPECT_EQ(*played, (std::vector<nlohmann::json>{5, 5, 5, 7, 7, 7}));
  EXPECT_EQ(*replayed, *played);
  EXPECT_TRUE(lastReply(replies)["state"]["replay"]["diverged"].is_null());
}

// A key given before the first frame (ready, press Enter, step) reaches frame
// 0's systems on replay too, not frame 1's.
TEST(PlaySession, AKeyGivenBeforeTheFirstFrameReplaysBeforeIt) {
  TempDir game;
  writeOneSceneGame(game);
  const auto play = game.path() / "play";
  driveGame(game, "step 2\n", {640, 360}, play, {}, [](Engine& e) {
    e.agent().recordInput({{"type", "key"}, {"name", "Enter"}, {"down", true}});  // as the inputs module does on `down Enter`
  });
  auto arrived = std::make_shared<bool>(false);
  auto seen = std::make_shared<std::vector<bool>>();
  driveGame(game, "step 2\n", {640, 360}, {}, play, [&](Engine& e) {
    e.getEventBus().subscribe<events::NamedKey>(EVT_NamedKey, [arrived](const events::NamedKey& k) {
      *arrived = std::string(k.name) == "Enter" && k.down;
    });
    e.getWorld().registerSystem<KeyProbe>(arrived, seen);
  });
  EXPECT_EQ(*seen, (std::vector<bool>{true, true}));
}

// A replay whose session values differ from its recording's diverges, even
// when its entities match; a play from before session hashes were kept doesn't.
TEST(PlaySession, AReplayWithOtherSessionValuesDiverges) {
  TempDir game;
  writeOneSceneGame(game);
  const auto play = game.path() / "play";
  driveGame(game, "step 31\n", {640, 360}, play);
  const std::string commands = "set score 999\nstep 31\nstate replay\n";
  EXPECT_EQ(lastReply(driveGame(game, commands, {640, 360}, {}, play))["state"]["replay"]["diverged"], 0);

  std::ifstream in(play / "timeline.jsonl");
  std::string old;
  for (std::string line; std::getline(in, line);) {
    auto sample = nlohmann::json::parse(line);
    sample.erase("sessionHash");
    old += sample.dump() + "\n";
  }
  in.close();
  std::ofstream(play / "timeline.jsonl") << old;
  EXPECT_TRUE(lastReply(driveGame(game, commands, {640, 360}, {}, play))["state"]["replay"]["diverged"].is_null());
}

// A marker is a moment of the game: before the first frame there's none (and
// no picture of it), so the driver says to step first.
TEST(PlaySession, AMarkerBeforeTheFirstFrameIsRefused) {
  TempDir game;
  writeOneSceneGame(game);
  const auto play = game.path() / "play";
  const std::string replies = driveGame(game, "marker zero\nstep 1\nmarker one\n", {640, 360}, play);
  std::istringstream lines(replies);
  std::vector<nlohmann::json> parsed;
  for (std::string line; std::getline(lines, line);) parsed.push_back(nlohmann::json::parse(line));
  ASSERT_EQ(parsed.size(), 4u);
  EXPECT_EQ(parsed[1]["ok"], false);
  EXPECT_NE(parsed[1]["error"].get<std::string>().find("step first"), std::string::npos);
  EXPECT_EQ(parsed[3]["marker"], 1);
  std::ifstream in(play / "session.json");
  EXPECT_EQ(nlohmann::json::parse(in)["markers"].size(), 1u);
}

// F8 with no recording says why: the recorder couldn't start, or a reload ended it.
TEST(PlaySession, AMarkerWithoutARecordingSaysWhy) {
  TempDir game;
  writeOneSceneGame(game);
  auto noticeAfter = [&](const std::filesystem::path& record, const std::string& commands) {
    std::string notice;
    driveGame(game, "", {640, 360}, record, {}, [&](Engine& engine) {  // the notice, read before shutdown
      std::istringstream in(commands);
      std::ostringstream out;
      engine.agent().drive(in, out);
      notice = engine.notice();
    });
    return notice;
  };
  const auto blocked = game.path() / "plays";
  std::ofstream(blocked) << "a file, not a folder";
  const std::string failed = noticeAfter(blocked / "p", "step 1\nmarker\n");
  EXPECT_NE(failed.find("couldn't start"), std::string::npos) << failed;
  EXPECT_EQ(failed.find("ended at a reload"), std::string::npos) << failed;

  const std::string reloaded = noticeAfter(game.path() / "play", "step 1\nreload\nmarker\n");
  EXPECT_NE(reloaded.find("ended at a reload"), std::string::npos) << reloaded;
}

// A play an exception ends says it crashed; nothing is recorded after its end
// (the shutdown's last events).
TEST(PlaySession, ACrashEndsThePlayAsCrashed) {
  TempDir dir;
  const auto path = dir.path() / "s";
  const nlohmann::json empty = nlohmann::json::object();
  const session::LazyState state = [&]() -> const nlohmann::json& { return empty; };
  try {
    session::Recorder recorder(path, {}, "");
    recorder.frameDone(0, 1.0f / 60.0f, state);
    throw std::runtime_error("a script's bug");
  } catch (const std::runtime_error&) {
  }
  EXPECT_EQ(session::Playback(path).meta()["ended"], "crashed");

  const auto quit = dir.path() / "q";
  {
    session::Recorder recorder(quit, {}, "");
    recorder.frameDone(0, 1.0f / 60.0f, state);
    recorder.end();
    recorder.input({{"type", "move"}, {"x", 1.0f}, {"y", 2.0f}});
  }
  const session::Playback played(quit);
  EXPECT_EQ(played.meta()["ended"], "quit");
  EXPECT_TRUE(played.eventsBefore(1).empty());
}

namespace {

// Something that moves with time: a session value each step, from its dt and a held key.
struct Mover : System {
  Engine& engine;
  std::shared_ptr<bool> held;
  Mover(Engine& e, std::shared_ptr<bool> h) : engine(e), held(std::move(h)) {}
  void update(World&, float dt) override {
    const double x = engine.getSession().getJson("x").value_or(0.0).get<double>();
    engine.getSession().setJson("x", x + dt * (*held ? 300.0 : 100.0));
  }
};

void addMover(Engine& engine) {
  auto held = std::make_shared<bool>(false);
  engine.getEventBus().subscribe<events::NamedKey>(EVT_NamedKey, [held](const events::NamedKey& k) {
    if (std::string(k.name) == "Right") *held = k.down;
  });
  engine.getWorld().registerSystem<Mover>(engine, held);
}

std::string fileText(const std::filesystem::path& path) {
  std::ifstream in(path);
  return std::string((std::istreambuf_iterator<char>(in)), {});
}

}  // namespace

// Frames are fixed steps: the same play on a 60 Hz display and a 144 Hz one
// runs the same steps and makes the same timeline, and replays the same on a 30 Hz one.
TEST(PlaySession, APlayIsTheSameAtAnyDisplayRate) {
  TempDir game;
  writeOneSceneGame(game);
  auto playAt = [&](float displayDt, const std::string& name, const std::filesystem::path& replayOf = {}) {
    EngineOptions options;
    options.dev = DevOptions{};
    options.dev.fixedDt = displayDt;
    options.dev.exitAfterFrames = 95;
    options.dev.playSession = replayOf;
    options.dev.recordDir = game.path() / name;
    if (replayOf.empty()) options.dev.saveDir = game.path() / "save";
    Engine engine(game.path(), ".jm.json", options);
    engine.initialize();
    addMover(engine);
    if (replayOf.empty()) {  // Right held from the start, as the inputs module gives it
      events::NamedKey right{{}, true};
      std::string("Right").copy(right.name, sizeof(right.name) - 1);
      engine.getEventBus().emit(EVT_NamedKey, right);
      engine.agent().recordInput({{"type", "key"}, {"name", "Right"}, {"down", true}});
    }
    engine.run();
    return fileText(game.path() / name / "timeline.jsonl");
  };
  const std::string at60 = playAt(1.0f / 60.0f, "at60");
  EXPECT_EQ(std::count(at60.begin(), at60.end(), '\n'), 5);  // frames 0, 30, 60, 90 and the last, 94
  EXPECT_NE(at60.find("\"x\":"), std::string::npos);
  EXPECT_EQ(playAt(1.0f / 144.0f, "at144"), at60);
  EXPECT_EQ(playAt(1.0f / 30.0f, "replayed", game.path() / "at144"), at60);
}

// The step rate is the game's (.jm.json config.stepRate); a drawn frame runs
// the steps its time brings due and draws between the last two.
TEST(PlaySession, DrawnFramesRunTheStepsTheyBringDue) {
  TempDir game;
  game.writeFile(".jm.json", R"({"name": "Rate", "entryScene": "scenes/main.scene.json",
                                "scenes": ["scenes/main.scene.json"], "assets": [], "config": {"stepRate": 50}})");
  game.writeFile("scenes/main.scene.json", R"({"name": "main", "entities": []})");
  EngineOptions options;
  options.dev = DevOptions{};
  options.dev.saveDir = game.path() / "save";
  Engine engine(game.path(), ".jm.json", options);
  engine.initialize();
  engine.setProfiling(true);
  EXPECT_FLOAT_EQ(engine.stepSeconds(), 0.02f);
  engine.frame(0.02f);
  EXPECT_EQ(engine.frameCount(), 1u);
  EXPECT_EQ(engine.drawFraction(), 1.0f);
  engine.frame(0.005f);  // a quarter step later: the next is due, drawn a quarter of the way to it
  EXPECT_EQ(engine.frameCount(), 2u);
  EXPECT_NEAR(engine.drawFraction(), 0.25f, 1e-4f);
  engine.frame(0.005f);
  EXPECT_EQ(engine.frameCount(), 2u);
  EXPECT_NEAR(engine.drawFraction(), 0.5f, 1e-4f);
  EXPECT_EQ(engine.profiler()->report()["frames"], 3);  // the profiler's frames are drawn ones, steps or none
  engine.frame(0.045f);  // the step half due, and one more; three quarters to the one after
  EXPECT_EQ(engine.frameCount(), 4u);
  EXPECT_NEAR(engine.drawFraction(), 0.75f, 1e-4f);
  engine.stepOnce();
  EXPECT_EQ(engine.frameCount(), 5u);
  EXPECT_EQ(engine.drawFraction(), 1.0f);
}

// A step is never longer than physics moves in one go (0.05 s), so scripts and bodies keep time.
TEST(PlaySession, AStepRateBelowTwentyIsTwenty) {
  for (const int rate : {10, 1}) {
    TempDir game;
    game.writeFile(".jm.json", R"({"name": "Slow", "entryScene": "scenes/main.scene.json", "scenes": ["scenes/main.scene.json"],
                                  "assets": [], "config": {"stepRate": )" + std::to_string(rate) + "}}");
    game.writeFile("scenes/main.scene.json", R"({"name": "main", "entities": []})");
    EngineOptions options;
    options.dev = DevOptions{};
    options.dev.saveDir = game.path() / "save";
    Engine engine(game.path(), ".jm.json", options);
    engine.initialize();
    EXPECT_FLOAT_EQ(engine.stepSeconds(), 0.05f) << "stepRate " << rate;
  }
}

// Plays made before hitches were recorded clamped keep the raw dt: a replay
// still advances at most kMaxDeltaTime, as the game did then.
TEST(PlaySession, AnOldPlaysRawHitchReplaysClamped) {
  TempDir game;
  writeOneSceneGame(game);
  auto run = [&](const std::filesystem::path& replayOf) {
    EngineOptions options;
    options.dev = DevOptions{};
    options.dev.fixedDt = 1.0f / 60.0f;
    options.dev.exitAfterFrames = replayOf.empty() ? 2 : 1;
    options.dev.playSession = replayOf;
    if (replayOf.empty()) {
      options.dev.recordDir = game.path() / "play";
      options.dev.saveDir = game.path() / "save";
    }
    Engine engine(game.path(), ".jm.json", options);
    engine.initialize();
    addMover(engine);
    engine.run();
    return engine.getSession().getJson("x").value_or(0.0).get<double>();
  };
  run({});
  {
    std::fstream frames(game.path() / "play" / "frames.bin", std::ios::binary | std::ios::in | std::ios::out);
    const float hitch = 0.25f;
    frames.write(reinterpret_cast<const char*>(&hitch), sizeof(hitch));
  }
  EXPECT_NEAR(run(game.path() / "play"), 10.0, 1e-4);  // 0.1 s at 100 a second
}

namespace {

void writeHeroGame(TempDir& game) {
  game.writeFile(".jm.json", R"({"name": "Held", "entryScene": "scenes/main.scene.json",
                                "scenes": ["scenes/main.scene.json"], "assets": []})");
  game.writeFile("scenes/main.scene.json", R"({"name": "main", "entities": [
      {"name": "Hero", "components": {}, "children": [{"name": "Hat", "components": {}}]}]})");
  game.writeFile("prefabs/rock.prefab.json", R"({"components": {}, "children": [{"name": "Moss", "components": {}}]})");
}

EntityId tagged(Engine& engine, const std::string& tag) { return *engine.getWorld().findWithTag(tag).begin(); }

}  // namespace

// F8's moment: the run held (no steps, sound or devices for the game) while
// entities are picked, saved as a marker with them, each with its origin and
// state; then the run goes on. The held time isn't in the play.
TEST(PlaySession, AMomentIsAMarkerWithWhatWasPicked) {
  TempDir game;
  writeHeroGame(game);
  const auto play = game.path() / "play";
  nlohmann::json picks, saved, again;
  driveGame(game, "", {640, 360}, play, {}, [&](Engine& engine) {
    AgentTools& tools = engine.agent();
    auto& run = dynamic_cast<AgentRun&>(tools);
    tools.command("step 3");
    EXPECT_TRUE(tools.moment());  // F8
    EXPECT_TRUE(engine.held());
    EXPECT_TRUE(engine.devicesMuted());
    EXPECT_EQ(tools.beforeFrame(), 0.0f);  // no game time passes
    // Where each is drawn (the moments module says, in a window): kept with the pick.
    run.setDescriber([](const AgentRun::Pick&) { return nlohmann::json{{"rect", {0.25, 0.5, 0.1, 0.2}}}; });
    using Kind = AgentRun::Pick::Kind;
    run.pick({.entity = tagged(engine, "Hero")}, false);
    run.pick({.entity = tagged(engine, "Hat")}, true);
    run.pick({.entity = tagged(engine, "Hat")}, true);  // shift-clicked again: taken away
    run.pick({.kind = Kind::Point, .lo = {12.5f, 3.0f}, .hi = {12.5f, 3.0f}}, true);
    run.pick({.kind = Kind::Area, .lo = {-4.0f, 0.0f}, .hi = {40.0f, 20.0f}}, true);
    picks = tools.command("picks");
    saved = tools.command("save look at him");
    EXPECT_FALSE(engine.held());
    EXPECT_EQ(tools.beforeFrame(), 1.0f);
    again = tools.command("save");
  });
  EXPECT_EQ(saved["marker"], 1);
  EXPECT_EQ(again["ok"], false);  // not held any more
  ASSERT_EQ(picks["picks"].size(), 3u);
  std::ifstream in(play / "session.json");
  const nlohmann::json meta = nlohmann::json::parse(in);
  EXPECT_EQ(meta["format"], session::kFormat);
  EXPECT_EQ(meta["frames"], 3);
  const nlohmann::json& marker = meta["markers"][0];
  EXPECT_EQ(marker["frame"], 2);
  EXPECT_EQ(marker["note"], "look at him");
  ASSERT_EQ(marker["picks"].size(), 3u);
  const nlohmann::json& pick = marker["picks"][0];
  EXPECT_EQ(pick["kind"], "entity");
  EXPECT_EQ(pick["entity"], picks["picks"][0]["entity"]);
  EXPECT_EQ(pick["origin"], (nlohmann::json{{"from", "scene"}, {"scene", "scenes/main.scene.json"}, {"entry", "0"}, {"name", "Hero"}}));
  EXPECT_EQ(pick["tags"], nlohmann::json::array({"Hero"}));
  EXPECT_TRUE(pick["state"].is_object());
  EXPECT_EQ(pick["rect"], (nlohmann::json{0.25, 0.5, 0.1, 0.2}));
  // A point and an area: where in the world, in which scene (and what's there: the moments module says).
  const nlohmann::json& point = marker["picks"][1];
  EXPECT_EQ(point["kind"], "point");
  EXPECT_EQ(point["at"], (nlohmann::json{12.5, 3.0}));
  EXPECT_EQ(point["scene"], "scenes/main.scene.json");
  EXPECT_EQ(point["rect"], (nlohmann::json{0.25, 0.5, 0.1, 0.2}));
  EXPECT_EQ(marker["picks"][2]["kind"], "area");
  EXPECT_EQ(marker["picks"][2]["area"], (nlohmann::json{-4.0, 0.0, 40.0, 20.0}));
}

// Every entity says where it came from: a scene's entry (children by their
// entry keys) or a spawn (its prefab's children as parts of it).
TEST(PlaySession, EntitiesKnowWhereTheyCameFrom) {
  TempDir game;
  writeHeroGame(game);
  nlohmann::json entities;
  EntityId rock{};
  driveGame(game, "", {640, 360}, {}, {}, [&](Engine& engine) {
    rock = engine.getSpawner().spawn("prefabs/rock.prefab.json", 1.0f, 2.0f);
    engine.agent().command("step 2");
    entities = engine.agent().command("state entities")["state"]["entities"];
  });
  std::map<std::string, nlohmann::json> origins;
  for (const nlohmann::json& e : entities) {
    origins[e["tags"].empty() ? "rock" : e["tags"][0].get<std::string>()] = e.value("origin", nlohmann::json());
  }
  EXPECT_EQ(origins["Hat"], (nlohmann::json{{"from", "scene"}, {"scene", "scenes/main.scene.json"}, {"entry", "0/c0"}, {"name", "Hat"}}));
  const nlohmann::json spawned = {{"from", "spawn"}, {"prefab", "prefabs/rock.prefab.json"}, {"frame", 0}};
  EXPECT_EQ(origins["rock"], spawned);
  nlohmann::json part = spawned;
  part["in"] = {rock.index, rock.generation};
  EXPECT_EQ(origins["Moss"], part);
}

// A held frame is drawn as the last step ended, not between two: what's
// picked is where it's drawn.
TEST(PlaySession, AHeldFrameShowsTheLastStep) {
  TempDir game;
  writeHeroGame(game);
  EngineOptions options;
  options.dev = DevOptions{};
  options.dev.saveDir = game.path() / "save";
  Engine engine(game.path(), ".jm.json", options);
  engine.initialize();
  engine.frame(engine.stepSeconds() * 1.25f);
  EXPECT_LT(engine.drawFraction(), 1.0f);
  dynamic_cast<AgentRun&>(engine.agent()).hold();
  engine.frame(0.0f);
  EXPECT_EQ(engine.frameCount(), 2u);
  EXPECT_EQ(engine.drawFraction(), 1.0f);
}

namespace {

// A changed game's new script: `does` runs at each update with its count (1 first).
struct Changed : System {
  Engine& engine;
  std::function<void(Engine&, int)> does;
  int frames = 0;
  Changed(Engine& e, std::function<void(Engine&, int)> d) : engine(e), does(std::move(d)) {}
  void update(World&, float) override { does(engine, ++frames); }
};

void writeTwoSceneGame(TempDir& game) {
  game.writeFile(".jm.json", R"({"name": "Driven", "entryScene": "scenes/main.scene.json",
                                "scenes": ["scenes/main.scene.json", "scenes/town.scene.json"], "assets": []})");
  // An entity each: replays tell the scenes apart by their entities.
  game.writeFile("scenes/main.scene.json", R"({"name": "main", "entities": [{"name": "gate", "components": {}}]})");
  game.writeFile("scenes/town.scene.json", R"({"name": "town", "entities": [{"name": "well", "components": {}}]})");
}

}  // namespace

// A resume at a marker (JM_PLAY_MARKER) lands there: where the replay got
// if that's the marker's scene (its values, changed or not, stand), else the
// marker's scene loaded with the values then, once the scene has settled.
// The new play replays the same either way.
TEST(PlaySession, AResumeLandsAtItsMarker) {
  TempDir game;
  writeTwoSceneGame(game);
  const auto play = game.path() / "play";
  driveGame(game, "step 3\nscene scenes/town.scene.json\nset score 5\nstep 3\nmarker here\nstep 3\n", {640, 360}, play);
  std::ifstream meta(play / "session.json");
  const int marked = nlohmann::json::parse(meta)["markers"][0]["frame"].get<int>();
  const std::string pastIt = "step " + std::to_string(marked + 60);  // the handover, and a fade after it
  const auto atMarker = [](const std::filesystem::path& p, int n) {
    std::ifstream in(p / "session.json");
    const uint64_t frame = nlohmann::json::parse(in)["markers"][n - 1]["frame"].get<uint64_t>();
    return [frame, n](DevOptions& dev) {
      dev.playUntil = frame + 1;
      dev.playMarker = n;
      dev.afterReplay = DevOptions::AfterReplay::Live;
    };
  };
  const auto changed = [](std::function<void(Engine&, int)> does) {
    return [does](Engine& e) { e.getWorld().registerSystem<Changed>(e, does); };
  };
  const auto bonus = [](Engine& e, int frame) {
    if (frame == 1) e.getSession().setJson("bonus", 1);
  };
  const auto resume = [&](const std::string& name, const std::function<void(Engine&)>& setup,
                          const std::filesystem::path& from = {}, int marker = 1) {
    const auto of = from.empty() ? play : from;
    return lastReply(driveGame(game, pastIt + "\nstate\n", {640, 360}, game.path() / name, of, setup, atMarker(of, marker)))["state"];
  };
  const auto given = [&](const std::string& name) { return fileText(game.path() / name / "inputs.jsonl").find("\"session\"") != std::string::npos; };
  const auto replays = [&](const std::string& name, const std::function<void(Engine&)>& setup) {
    const nlohmann::json state = lastReply(driveGame(game, pastIt + "\nstate\n", {640, 360}, {}, game.path() / name, setup))["state"];
    EXPECT_TRUE(state["replay"]["diverged"].is_null()) << name;
    return state["scene"];
  };

  nlohmann::json state = resume("same", {});
  EXPECT_EQ(state["scene"], "scenes/town.scene.json");
  EXPECT_EQ(state["session"], (nlohmann::json{{"score", 5}}));
  EXPECT_FALSE(given("same"));

  state = resume("values", changed(bonus));
  EXPECT_EQ(state["session"], (nlohmann::json{{"bonus", 1}, {"score", 5}}));  // in its scene: the change is theirs to try
  EXPECT_FALSE(given("values"));

  // Spelled another way, it's the same scene.
  resume("spelled", changed([](Engine& e, int frame) {
    if (frame == 5) e.getSceneManager().loadScene("scenes/./town.scene.json");
  }));
  EXPECT_FALSE(given("spelled"));

  // Sent back with a fade still running at the handover.
  const auto fadesBack = changed([&](Engine& e, int frame) {
    bonus(e, frame);
    if (frame == 5) e.getSceneManager().transitionTo("scenes/main.scene.json", {.duration = 0.5f});
  });
  state = resume("faded", fadesBack);
  EXPECT_EQ(state["scene"], "scenes/town.scene.json");
  EXPECT_EQ(state["session"], (nlohmann::json{{"score", 5}}));  // the values then
  EXPECT_TRUE(given("faded"));
  EXPECT_EQ(replays("faded", fadesBack), "scenes/town.scene.json");

  // Sent back by a change queued on the last replayed frame (as scripts ask), applied after the handover.
  state = resume("queued", changed([&](Engine& e, int frame) {
    if (frame == marked + 1) e.getSceneManager().requestLoad("scenes/main.scene.json");
  }));
  EXPECT_EQ(state["scene"], "scenes/town.scene.json");

  // Two markers on one frame: the one asked for.
  const auto twice = game.path() / "twice";
  driveGame(game, "step 3\nmarker one\nscene scenes/town.scene.json\nmarker two\nstep 3\n", {640, 360}, twice);
  EXPECT_EQ(resume("second", {}, twice, 2)["scene"], "scenes/town.scene.json");

  // A marker's scene that no longer loads (the old game's script went there;
  // the new one doesn't): the recording ends there, replayable.
  const auto old = game.path() / "old";
  driveGame(game, "step 6\nmarker there\nstep 3\n", {640, 360}, old, {}, changed([](Engine& e, int frame) {
    if (frame == 3) e.getSceneManager().loadScene("scenes/town.scene.json");
  }));
  game.writeFile("scenes/town.scene.json", "{ not json");
  resume("broken", {}, old);
  std::ifstream broken(game.path() / "broken" / "session.json");
  EXPECT_EQ(nlohmann::json::parse(broken)["frames"], 6);
  replays("broken", {});
}
