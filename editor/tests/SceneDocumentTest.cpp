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
  EXPECT_TRUE(doc->takeDiskVersion(*disk).empty());
  // Hero and Slime went on disk (untouched here); Dragon came; Bat, unsaved here, stays.
  EXPECT_EQ(names(*doc), (std::vector<std::string>{"Dragon", "Bat"}));
  EXPECT_TRUE(doc->dirty());
  EXPECT_EQ(doc->undoLabel(), "Change on Disk");
  doc->undo();
  EXPECT_EQ(names(*doc), (std::vector<std::string>{"Hero", "Slime", "Bat"}));  // the unsaved edits as they were
  std::filesystem::remove_all(root);
}

namespace {

Json at(const std::string& name, int x) {
  return Json{{"name", name}, {"components", {{"TransformComponent", {{"position", {x, 0, 0}}}}}}};
}

int xOf(const SceneDocument& doc, EntityUid uid) { return doc.find(uid)->at("components").at("TransformComponent").at("position")[0].get<int>(); }

EntityUid uidNamed(const SceneDocument& doc, const std::string& name) {
  for (size_t i = 0; i < doc.size(); ++i)
    if (doc.entity(i).value("name", "") == name) return doc.uid(i);
  return 0;
}

}  // namespace

// Unsaved edits here and an agent's edit on disk to different entities both survive, and every entity keeps its id.
TEST(SceneDocument, AChangeOnDiskMergesEntityByEntity) {
  SceneDocument doc = SceneDocument::create("scenes/a.scene.json");
  doc.addEntities({at("Player", 0), at("Coin", 10)}, "Add");
  const EntityUid player = uidNamed(doc, "Player"), coin = uidNamed(doc, "Coin");
  Json saved = Json::parse(doc.serialized());
  // As if loaded from that file: the disk's version is the merge's base.
  doc.takeDiskVersion(saved);
  ASSERT_FALSE(doc.dirty());

  doc.editEntity(coin, "Nudge", [](Json& e) { e["components"]["TransformComponent"]["position"][0] = 11; });
  Json disk = saved;
  disk["entities"][0]["components"]["TransformComponent"]["position"][0] = 50;  // the agent moved Player
  EXPECT_TRUE(doc.takeDiskVersion(disk).empty());
  EXPECT_EQ(doc.find(player) ? xOf(doc, player) : -1, 50);
  EXPECT_EQ(doc.find(coin) ? xOf(doc, coin) : -1, 11);
  EXPECT_TRUE(doc.dirty());  // the nudge isn't on disk yet
}

TEST(SceneDocument, BothSidesChangingAnEntityLetsTheFileWinAndSaysSo) {
  SceneDocument doc = SceneDocument::create("scenes/a.scene.json");
  const EntityUid hero = doc.addEntity(at("Hero", 0), "Add Hero");
  const Json saved = Json::parse(doc.serialized());
  doc.takeDiskVersion(saved);

  doc.editEntity(hero, "Nudge", [](Json& e) { e["components"]["TransformComponent"]["position"][0] = 1; });
  Json disk = saved;
  disk["entities"][0]["components"]["TransformComponent"]["position"][0] = 99;
  EXPECT_EQ(doc.takeDiskVersion(disk), (std::vector<std::string>{"Hero"}));
  EXPECT_EQ(xOf(doc, hero), 99);
  EXPECT_FALSE(doc.dirty());  // what's here is what's on disk
  doc.undo();
  EXPECT_EQ(xOf(doc, hero), 1);  // yours, one Undo back
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

TEST(SceneDocument, SpotReferencesNameAPlaceInIt) {
  const SceneDocument doc = SceneDocument::create("scenes/a.scene.json");
  EXPECT_EQ(doc.spotReference({120.4f, -39.6f}), "scenes/a.scene.json@120,-40");
}

// Loaded from `entities` as if from its file (the merge's base), with nothing unsaved.
SceneDocument sceneOnDisk(std::vector<Json> entities) {
  SceneDocument doc = SceneDocument::create("scenes/a.scene.json");
  doc.addEntities(std::move(entities), "Add");
  doc.takeDiskVersion(Json::parse(doc.serialized()));
  return doc;
}

TEST(SceneDocument, AnEntityDeletedOnDiskStaysDeletedEvenIfChangedHere) {
  SceneDocument doc = sceneOnDisk({at("Hero", 0), at("Coin", 5)});
  const Json saved = Json::parse(doc.serialized());
  doc.editEntity(uidNamed(doc, "Hero"), "Nudge", [](Json& e) { e["components"]["TransformComponent"]["position"][0] = 1; });
  Json disk = saved;
  disk["entities"].erase(0);
  EXPECT_EQ(doc.takeDiskVersion(disk), (std::vector<std::string>{"Hero"}));
  EXPECT_EQ(names(doc), (std::vector<std::string>{"Coin"}));
}

TEST(SceneDocument, SiblingsSharingANameMatchByOrderNotByTheirLabel) {
  SceneDocument doc = sceneOnDisk({at("Bat", 0), at("Bat", 1), at("Bat[2]", 2), at("Coin", 3)});
  const Json saved = Json::parse(doc.serialized());
  doc.editEntity(uidNamed(doc, "Bat[2]"), "Nudge", [](Json& e) { e["components"]["TransformComponent"]["position"][0] = 20; });
  Json disk = saved;
  disk["entities"][3]["components"]["TransformComponent"]["position"][0] = 30;
  EXPECT_TRUE(doc.takeDiskVersion(disk).empty());
  EXPECT_EQ(xOf(doc, uidNamed(doc, "Bat[2]")), 20);
  EXPECT_EQ(xOf(doc, uidNamed(doc, "Coin")), 30);
}

TEST(SceneDocument, AnOrderMadeHereStaysWhenTheFileKeptItsOrder) {
  SceneDocument doc = sceneOnDisk({at("A", 0), at("B", 1)});
  const Json saved = Json::parse(doc.serialized());
  doc.moveEntities({uidNamed(doc, "B")}, 0, 0, "Move B");
  Json disk = saved;
  disk["entities"][0]["components"]["TransformComponent"]["position"][0] = 9;
  EXPECT_TRUE(doc.takeDiskVersion(disk).empty());
  EXPECT_EQ(names(doc), (std::vector<std::string>{"B", "A"}));
  EXPECT_EQ(xOf(doc, uidNamed(doc, "A")), 9);
  EXPECT_TRUE(doc.dirty());
}

// A file broken on disk says why (for the banner) until it reads again.
TEST(SceneDocument, ABrokenFileOnDiskSaysWhyUntilItReads) {
  const auto root = std::filesystem::temp_directory_path() / ("jm_scene_broken_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root / "scenes");
  std::ofstream(root / ".jm.json") << R"({"name": "t"})";
  std::string error;
  auto opened = Project::open(root, error);
  ASSERT_TRUE(opened) << error;
  Project& project = *opened;
  const std::string path = "scenes/a.scene.json";
  ASSERT_TRUE(project.writeText(path, R"({"name": "a", "entities": []})", error));
  auto doc = SceneDocument::load(project, path, error);
  ASSERT_TRUE(doc) << error;
  auto touch = [&](const std::string& text, int seconds) {
    const auto later = std::filesystem::last_write_time(project.abs(path)) + std::chrono::seconds(seconds);
    ASSERT_TRUE(project.writeText(path, text, error));
    std::filesystem::last_write_time(project.abs(path), later);
  };
  touch("{\n  \"name\": \"a\",\n  \"entities\": [\n", 2);
  EXPECT_FALSE(doc->changedOnDisk(project));
  EXPECT_NE(doc->diskProblem().find("line"), std::string::npos) << doc->diskProblem();
  touch(R"({"name": "a", "entities": [{"name": "Hero"}]})", 4);
  EXPECT_TRUE(doc->changedOnDisk(project));
  EXPECT_TRUE(doc->diskProblem().empty());
  std::filesystem::remove_all(root);
}
