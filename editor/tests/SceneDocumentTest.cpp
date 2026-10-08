#include <gtest/gtest.h>

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
