#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "Project.hpp"

TEST(Project, ANormalMapIsItsImagesNotAPictureOfItsOwn) {
  EXPECT_EQ(normalMapPath("assets/hero.png"), "assets/hero.normal.png");
  EXPECT_EQ(normalMapPath("assets/hero.normal.png"), "");
  EXPECT_EQ(normalMapPath("assets/hero.jpg"), "");
  EXPECT_TRUE(assetMatches("assets/hero.png", {".png"}));
  EXPECT_FALSE(assetMatches("assets/hero.normal.png", {".png"}));
  EXPECT_TRUE(assetMatches("assets/hero.normal.png", {".normal.png"}));
  EXPECT_TRUE(assetMatches("assets/hero.normal.png", {}));  // any file
}

TEST(Project, AnAtlasSourcesRegionIsWhatJmBuildNamesIt) {
  EXPECT_EQ(atlasRegion("art/hero.png"), "hero");
  EXPECT_EQ(atlasRegion("art/hero.json"), "hero_0");  // an Aseprite sheet: its first frame
  EXPECT_EQ(atlasRegion("art/hero.JSON"), "hero_0");
}

// jm settings_update (or any other program) writing .jm.json while the editor
// is open: the editor's next save keeps that change, and the editor's own.
TEST(Project, SavingTheManifestKeepsAnotherProgramsChange) {
  const auto root = std::filesystem::temp_directory_path() / ("jm_manifest_merge_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  std::ofstream(root / ".jm.json") << R"({"name": "t", "window": {"width": 640}, "version": "1", "old": true})";
  std::string error;
  auto opened = Project::open(root, error);
  ASSERT_TRUE(opened) << error;
  Project& project = *opened;

  ASSERT_TRUE(project.writeText(".jm.json", R"({"name": "t", "window": {"width": 1280}, "version": "1", "old": true})", error));
  project.manifest()["entryScene"] = "scenes/b.scene.json";
  project.manifest()["version"] = "2";
  project.manifest().erase("old");
  ASSERT_TRUE(project.saveManifest(error)) << error;

  const Json saved = Json::parse(project.readText(".jm.json"));
  EXPECT_EQ(saved["window"]["width"], 1280);  // the other program's
  EXPECT_EQ(saved["entryScene"], "scenes/b.scene.json");  // the editor's
  EXPECT_EQ(saved["version"], "2");
  EXPECT_FALSE(saved.contains("old"));
  EXPECT_EQ(project.manifest(), saved);  // the editor now holds what it wrote

  // Saving again with nothing changed writes nothing over a newer outside change.
  ASSERT_TRUE(project.writeText(".jm.json", R"({"name": "renamed", "window": {"width": 1280}, "version": "2", "entryScene": "scenes/b.scene.json"})", error));
  ASSERT_TRUE(project.saveManifest(error)) << error;
  EXPECT_EQ(Json::parse(project.readText(".jm.json"))["name"], "renamed");

  // Inside one setting too: jm's window width and the editor's vsync both stay.
  ASSERT_TRUE(project.writeText(
      ".jm.json",
      R"({"name": "renamed", "window": {"width": 1920}, "version": "2", "entryScene": "scenes/b.scene.json"})", error));
  project.manifest()["window"]["vsync"] = false;
  ASSERT_TRUE(project.saveManifest(error)) << error;
  const Json window = Json::parse(project.readText(".jm.json"))["window"];
  EXPECT_EQ(window["width"], 1920);
  EXPECT_EQ(window["vsync"], false);

  // No lock, no save: writing unlocked could undo jm's write mid-way.
  std::filesystem::remove(root / "..jm.json.lock");
  std::filesystem::create_directories(root / "..jm.json.lock" / "in-the-way");  // can't be opened as a lock
  project.manifest()["version"] = "3";
  EXPECT_FALSE(project.saveManifest(error));
  EXPECT_EQ(Json::parse(project.readText(".jm.json"))["version"], "2");
  std::filesystem::remove_all(root);
}

TEST(Project, MergeChangesTakesEachSidesOwnChanges) {
  const Json base = Json::parse(R"({"a": 1, "o": {"x": 1, "y": 1}, "gone": 1})");
  const Json mine = Json::parse(R"({"a": 2, "o": {"x": 2, "y": 1}, "new": 1})");
  const Json theirs = Json::parse(R"({"a": 1, "o": {"x": 1, "y": 3}, "gone": 1, "theirs": 1})");
  EXPECT_EQ(mergeChanges(base, mine, theirs), Json::parse(R"({"a": 2, "o": {"x": 2, "y": 3}, "theirs": 1, "new": 1})"));
  EXPECT_EQ(mergeChanges(base, base, theirs), theirs);  // nothing changed here
  // A section neither had, added on both sides: each side's settings in it stay.
  EXPECT_EQ(mergeChanges(Json::object(), Json::parse(R"({"config": {"window": {"vsync": false}}})"),
                         Json::parse(R"({"config": {"window": {"width": 1920}}})")),
            Json::parse(R"({"config": {"window": {"width": 1920, "vsync": false}}})"));
}
