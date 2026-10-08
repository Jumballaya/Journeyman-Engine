#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "SceneDocument.hpp"

namespace {

Json named(const std::string& name) { return Json{{"name", name}, {"components", Json::object()}}; }

std::vector<std::string> names(const SceneDocument& doc) {
  std::vector<std::string> out;
  for (size_t i = 0; i < doc.size(); ++i) out.push_back(doc.entity(i).value("name", ""));
  return out;
}

}  // namespace

TEST(SceneDocument, UndoAndRedoWalkTheHistory) {
  SceneDocument doc = SceneDocument::create("scenes/a.scene.json");
  doc.addEntity(named("Hero"), "Add Hero");
  doc.addEntity(named("Slime"), "Add Slime");
  EXPECT_EQ(names(doc), (std::vector<std::string>{"Hero", "Slime"}));
  EXPECT_EQ(doc.undoLabel(), "Add Slime");

  doc.undo();
  EXPECT_EQ(names(doc), (std::vector<std::string>{"Hero"}));
  EXPECT_EQ(doc.redoLabel(), "Add Slime");
  doc.undo();
  EXPECT_TRUE(names(doc).empty());
  EXPECT_FALSE(doc.canUndo());

  doc.redo();
  doc.redo();
  EXPECT_EQ(names(doc), (std::vector<std::string>{"Hero", "Slime"}));
  EXPECT_FALSE(doc.canRedo());
}

TEST(SceneDocument, ANewEditDropsTheRedoBranch) {
  SceneDocument doc = SceneDocument::create("scenes/a.scene.json");
  doc.addEntity(named("Hero"), "Add Hero");
  doc.addEntity(named("Slime"), "Add Slime");
  doc.undo();
  doc.addEntity(named("Bat"), "Add Bat");
  EXPECT_FALSE(doc.canRedo());
  EXPECT_EQ(names(doc), (std::vector<std::string>{"Hero", "Bat"}));
}

// Dragging a field makes one undo step, not one per frame.
TEST(SceneDocument, EditsWithTheSameMergeKeyFold) {
  SceneDocument doc = SceneDocument::create("scenes/a.scene.json");
  const EntityUid hero = doc.addEntity(named("Hero"), "Add Hero");
  for (int x = 1; x <= 30; ++x) {
    doc.editEntity(hero, "Move Hero", [x](Json& e) { e["components"]["TransformComponent"]["position"] = {x, 0, 0}; },
                   "drag:hero");
  }
  EXPECT_EQ(doc.historyLabels(), (std::vector<std::string>{"Add Hero", "Move Hero"}));
  doc.undo();
  EXPECT_FALSE(doc.find(hero)->at("components").contains("TransformComponent"));
}

TEST(SceneDocument, AnEditThatChangesNothingIsNoStep) {
  SceneDocument doc = SceneDocument::create("scenes/a.scene.json");
  const EntityUid hero = doc.addEntity(named("Hero"), "Add Hero");
  doc.editEntity(hero, "Nothing", [](Json&) {});
  EXPECT_EQ(doc.historyLabels(), (std::vector<std::string>{"Add Hero"}));
}

TEST(SceneDocument, JumpToMovesThroughTheHistory) {
  SceneDocument doc = SceneDocument::create("scenes/a.scene.json");
  for (const char* n : {"A", "B", "C", "D"}) doc.addEntity(named(n), std::string("Add ") + n);
  doc.jumpTo(1);
  EXPECT_EQ(names(doc), (std::vector<std::string>{"A"}));
  doc.jumpTo(3);
  EXPECT_EQ(names(doc), (std::vector<std::string>{"A", "B", "C"}));
  doc.jumpTo(99);  // past the end: as far as it goes
  EXPECT_EQ(doc.historyPosition(), 4u);
}

// Undo follows nesting: removing a parent takes its children, and undo brings both back.
TEST(SceneDocument, UndoRestoresARemovedSubtree) {
  SceneDocument doc = SceneDocument::create("scenes/a.scene.json");
  const EntityUid hero = doc.addEntity(named("Hero"), "Add Hero");
  const EntityUid sword = doc.addEntity(named("Sword"), "Add Sword", hero);
  doc.removeEntities({hero}, "Delete Hero");
  EXPECT_EQ(doc.size(), 0u);
  doc.undo();
  ASSERT_EQ(doc.size(), 2u);
  EXPECT_EQ(doc.parentOf(sword), hero);
}

TEST(SceneDocument, ANeverSavedSceneIsDirty) {
  SceneDocument doc = SceneDocument::create("scenes/a.scene.json");
  EXPECT_TRUE(doc.dirty());  // never saved
  doc.addEntity(named("Hero"), "Add Hero");
  EXPECT_TRUE(doc.dirty());
}

// Long sessions keep a bounded history: the oldest steps go, the newest stay undoable.
TEST(SceneDocument, HistoryIsBounded) {
  SceneDocument doc = SceneDocument::create("scenes/a.scene.json");
  const EntityUid hero = doc.addEntity(named("Hero"), "Add Hero");
  for (int i = 0; i < 1000; ++i) doc.editEntity(hero, "Rename", [i](Json& e) { e["name"] = "Hero " + std::to_string(i); });
  EXPECT_LE(doc.historyLabels().size(), 400u);
  doc.undo();
  EXPECT_EQ(doc.find(hero)->value("name", ""), "Hero 998");
}

// Another program's edit to the open file comes in as an undoable step that
// counts as saved; the document's own saves don't count as changes.
TEST(SceneDocument, AnOutsideChangeBecomesAnUndoStep) {
  const auto root = std::filesystem::temp_directory_path() / ("jm_scene_disk_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root / "scenes");
  std::ofstream(root / ".jm.json") << R"({"name": "t"})";
  std::string error;
  auto opened = Project::open(root, error);
  ASSERT_TRUE(opened) << error;
  Project& project = *opened;
  ASSERT_TRUE(project.writeText("scenes/a.scene.json", R"({"name": "a", "entities": [{"name": "Hero"}]})", error));
  auto doc = SceneDocument::load(project, "scenes/a.scene.json", error);
  ASSERT_TRUE(doc) << error;

  doc->addEntity(named("Slime"), "Add Slime");
  ASSERT_TRUE(doc->save(project, error));
  EXPECT_FALSE(doc->changedOnDisk(project));  // our own save

  doc->addEntity(named("Bat"), "Add Bat");  // unsaved
  const auto later = std::filesystem::last_write_time(project.abs("scenes/a.scene.json")) + std::chrono::seconds(2);
  ASSERT_TRUE(project.writeText("scenes/a.scene.json", R"({"name": "a", "entities": [{"name": "Dragon"}]})", error));
  std::filesystem::last_write_time(project.abs("scenes/a.scene.json"), later);  // coarse clocks: make it visibly newer
  auto disk = doc->changedOnDisk(project);
  ASSERT_TRUE(disk);
  EXPECT_FALSE(doc->changedOnDisk(project));  // reported once
  doc->takeDiskVersion(*disk);
  EXPECT_EQ(names(*doc), (std::vector<std::string>{"Dragon"}));
  EXPECT_FALSE(doc->dirty());
  EXPECT_EQ(doc->undoLabel(), "Change on Disk");
  doc->undo();
  EXPECT_EQ(names(*doc), (std::vector<std::string>{"Hero", "Slime", "Bat"}));  // the unsaved edits, back
  std::filesystem::remove_all(root);
}

// References name an entity outside the editor: its file and path of names.
TEST(SceneDocument, ReferencesNameEntitiesByPath) {
  SceneDocument doc = SceneDocument::create("scenes/a.scene.json");
  const EntityUid hero = doc.addEntity(named("Hero"), "Add Hero");
  const EntityUid sword = doc.addEntity(named("Sword"), "Add Sword", hero);
  doc.addEntity(named("Bat"), "Add Bat");
  const EntityUid bat2 = doc.addEntity(named("Bat"), "Add Bat");
  EXPECT_EQ(doc.reference(hero), "scenes/a.scene.json#Hero");
  EXPECT_EQ(doc.reference(sword), "scenes/a.scene.json#Hero/Sword");
  EXPECT_EQ(doc.reference(bat2), "scenes/a.scene.json#Bat[2]");
}
