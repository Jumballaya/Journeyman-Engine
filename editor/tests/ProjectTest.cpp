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
  std::filesystem::remove_all(root);
}
