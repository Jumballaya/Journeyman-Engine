#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "EditorSession.hpp"

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

fs::path freshRoot() {
  const fs::path root = fs::temp_directory_path() / ("jm-editor-session-" + std::to_string(getpid()));
  fs::remove_all(root);
  return root;
}

std::optional<Json> read(const fs::path& file) {
  std::ifstream in(file);
  if (!in) return std::nullopt;
  return Json::parse(in);
}

}  // namespace

TEST(EditorSession, PublishesWhatsOpenAndUnsavedUnderThisProcessId) {
  const fs::path root = freshRoot();
  {
    EditorSession session(root);
    EXPECT_EQ(session.file(), root / ".jm" / ("editor-session-" + std::to_string(getpid()) + ".json"));
    EXPECT_FALSE(fs::exists(session.file()));  // nothing until the first publish

    session.publish({{"scenes/a.scene.json", "atlases/b.atlas.json"}, {"scenes/a.scene.json"}});
    const auto json = read(session.file());
    ASSERT_TRUE(json);
    EXPECT_EQ((*json)["open"], Json({"scenes/a.scene.json", "atlases/b.atlas.json"}));
    EXPECT_EQ((*json)["unsaved"], Json({"scenes/a.scene.json"}));
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    EXPECT_NEAR((*json)["updated"].get<double>(), static_cast<double>(now), 2.0);

    session.publish({{"scenes/a.scene.json"}, {}});  // saved, and the asset tab closed
    EXPECT_EQ((*read(session.file()))["unsaved"], Json::array());
    EXPECT_EQ((*read(session.file()))["open"], Json({"scenes/a.scene.json"}));
  }
  EXPECT_FALSE(fs::exists(root / ".jm" / ("editor-session-" + std::to_string(getpid()) + ".json")));
  fs::remove_all(root);
}

TEST(EditorSession, AnUnchangedSessionIsntRewrittenBeforeTheHeartbeat) {
  const fs::path root = freshRoot();
  EditorSession session(root);
  session.publish({{"scenes/a.scene.json"}, {}});
  fs::remove(session.file());
  session.publish({{"scenes/a.scene.json"}, {}});
  EXPECT_FALSE(fs::exists(session.file()));
  session.publish({{"scenes/a.scene.json"}, {"scenes/a.scene.json"}});
  EXPECT_TRUE(fs::exists(session.file()));
  fs::remove_all(root);
}

TEST(EditorSession, AFailedWriteIsSurvivedAndTheNextChangeLands) {
  const fs::path root = freshRoot();
  EditorSession session(root);
  fs::create_directories(session.file() / "blocker");  // a folder where the file goes: can't replace it
  session.publish({{"scenes/a.scene.json"}, {"scenes/a.scene.json"}});
  session.publish({{"scenes/a.scene.json"}, {}});
  fs::remove_all(session.file());
  session.publish({{"scenes/a.scene.json"}, {"scenes/a.scene.json"}});
  const auto json = read(session.file());
  ASSERT_TRUE(json);
  EXPECT_EQ((*json)["unsaved"], Json({"scenes/a.scene.json"}));
  fs::remove_all(root);
}

TEST(EditorSession, PublishesTheSceneAndSelectionAndThePersonsNotes) {
  const fs::path root = freshRoot();
  EditorSession session(root);
  session.publish({{"scenes/a.scene.json"}, {}, "scenes/a.scene.json", {"Player"}});
  session.pointOut("scenes/a.scene.json", {"Player"}, "jumps too low");
  session.publish({{"scenes/a.scene.json"}, {}, "scenes/a.scene.json", {"Player"}});  // a note is a change: written now
  auto json = read(session.file());
  ASSERT_TRUE(json);
  EXPECT_EQ((*json)["scene"], "scenes/a.scene.json");
  EXPECT_EQ((*json)["selection"], Json({"Player"}));
  ASSERT_EQ((*json)["notes"].size(), 1u);
  EXPECT_EQ((*json)["notes"][0]["id"], 1);
  EXPECT_EQ((*json)["notes"][0]["text"], "jumps too low");
  EXPECT_EQ((*json)["notes"][0]["entities"], Json({"Player"}));
  for (int i = 0; i < 25; ++i) session.pointOut("scenes/a.scene.json", {}, "note " + std::to_string(i));
  session.publish({{"scenes/a.scene.json"}, {}, "scenes/a.scene.json", {}});
  json = read(session.file());
  ASSERT_EQ((*json)["notes"].size(), 20u);  // the newest
  EXPECT_EQ((*json)["notes"].back()["id"], 26);
  fs::remove_all(root);
}

TEST(EditorSession, TakesTheAgentsWholeRequestsOnce) {
  const fs::path root = freshRoot();
  EditorSession session(root);
  EXPECT_EQ(session.inbox(), root / ".jm" / ("editor-session-" + std::to_string(getpid()) + ".in"));
  EXPECT_TRUE(session.takeRequests().empty());  // no inbox yet
  fs::create_directories(session.inbox().parent_path());
  std::ofstream(session.inbox(), std::ios::app) << "{\"scene\":\"scenes/b.scene.json\",\"entity\":\"Door\",\"note\":\"here\"}\nnot json\n{\"entity\":";
  auto shown = session.takeRequests();
  ASSERT_EQ(shown.size(), 1u);
  EXPECT_EQ(shown[0].scene, "scenes/b.scene.json");
  EXPECT_EQ(shown[0].entity, "Door");
  EXPECT_EQ(shown[0].note, "here");
  EXPECT_TRUE(session.takeRequests().empty());  // the half-written line waits
  std::ofstream(session.inbox(), std::ios::app) << "\"Key\"}\n";
  shown = session.takeRequests();
  ASSERT_EQ(shown.size(), 1u);
  EXPECT_EQ(shown[0].entity, "Key");
  EXPECT_EQ(shown[0].scene, "");
  fs::remove_all(root);
}
