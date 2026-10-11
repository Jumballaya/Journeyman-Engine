#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "Project.hpp"

namespace fs = std::filesystem;

namespace {

void write(const fs::path& file, const std::string& text) {
  fs::create_directories(file.parent_path());
  std::ofstream(file) << text;
}

}  // namespace

TEST(NewProject, ACopyOfAnExampleTakesItsSourcesAndTheNewName) {
  const fs::path root = fs::temp_directory_path() / ("jm_new_project_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const fs::path example = root / "demos" / "platformer";
  write(example / ".jm.json", R"({"name": "Platformer", "entryScene": "scenes/main.scene.json"})");
  write(example / "scenes" / "main.scene.json", R"({"entities": []})");
  write(example / "assets" / "scripts" / "pip.ts", "export function onUpdate(dt: f32): void {}");
  for (const char* skipped : {"build/game.jm", ".jm/plays/1/session.json", "assets/scripts/node_modules/x/index.js", "dist/game"})
    write(example / skipped, "x");

  std::string error;
  const fs::path game = root / "games" / "my-platformer";
  ASSERT_TRUE(copyProject(example, game, "my-platformer", error)) << error;
  EXPECT_TRUE(fs::exists(game / "scenes" / "main.scene.json"));
  EXPECT_TRUE(fs::exists(game / "assets" / "scripts" / "pip.ts"));
  EXPECT_FALSE(fs::exists(game / "build"));
  EXPECT_FALSE(fs::exists(game / ".jm"));
  EXPECT_FALSE(fs::exists(game / "dist"));
  EXPECT_FALSE(fs::exists(game / "assets" / "scripts" / "node_modules"));
  const auto copy = Project::open(game, error);
  ASSERT_TRUE(copy) << error;
  EXPECT_EQ(copy->name(), "my-platformer");
  EXPECT_EQ(Project::open(example, error)->name(), "Platformer");  // the original is untouched

  EXPECT_FALSE(copyProject(example, game, "again", error));  // never over a folder with files
  EXPECT_FALSE(copyProject(example, example / "copy", "nested", error));  // nor into itself
  EXPECT_FALSE(fs::exists(example / "copy"));
  fs::remove_all(root);
}

// A shared script library outside the example (dungeon's ../common) comes with the copy.
TEST(NewProject, ACopyTakesItsScriptLibrariesAlong) {
  const fs::path root = fs::temp_directory_path() / ("jm_new_project_libs_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const fs::path example = root / "demos" / "dungeon";
  write(example / ".jm.json", R"({"name": "Dungeon", "scriptLibraries": {"@demos/common": "../common", "local": "../dungeon/lib"}})");
  write(example / "lib" / "index.ts", "export const here = 1;");
  write(root / "demos" / "common" / "index.ts", "export const shared = 1;");
  write(root / "demos" / "common" / "node_modules" / "x" / "index.js", "x");

  std::string error;
  const fs::path game = root / "games" / "dungeon";
  ASSERT_TRUE(copyProject(example, game, "dungeon", error)) << error;
  EXPECT_TRUE(fs::exists(game / "libraries" / "demos-common" / "index.ts"));
  EXPECT_FALSE(fs::exists(game / "libraries" / "demos-common" / "node_modules"));
  EXPECT_TRUE(fs::exists(game / "lib" / "index.ts"));
  const Json libraries = Project::open(game, error)->manifest()["scriptLibraries"];
  EXPECT_EQ(libraries["@demos/common"], "libraries/demos-common");
  EXPECT_EQ(libraries["local"], "lib");  // inside the project: copied with it, named from the copy

  EXPECT_FALSE(copyProject(example, example, "itself", error));  // a copy never lands on its original
  EXPECT_EQ(Project::open(example, error)->name(), "Dungeon");
  EXPECT_FALSE(copyProject(example, root / "demos" / "common" / "game", "in-library", error));  // nor in what it copies
  EXPECT_FALSE(fs::exists(root / "demos" / "common" / "game"));
  write(example / ".jm.json", R"({"scriptLibraries": {"@my/common": "../common", "mine": "libraries/my-common"}})");
  write(example / "libraries" / "my-common" / "index.ts", "export const mine = 1;");
  EXPECT_FALSE(copyProject(example, root / "games" / "taken", "taken", error));  // never over the project's own files
  fs::remove_all(root);
}

#ifndef _WIN32
// A skipped link is never followed: its target may not even be readable.
TEST(NewProject, ACopySkipsLinksWithoutLookingAtTheirTargets) {
  const fs::path root = fs::temp_directory_path() / ("jm_new_project_link_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const fs::path example = root / "example";
  write(example / ".jm.json", R"({"name": "Linked"})");
  write(root / "locked" / "secret.txt", "x");
  fs::create_symlink(root / "locked" / "secret.txt", example / "secret.txt");
  fs::permissions(root / "locked", fs::perms::none);

  std::string error;
  EXPECT_NO_THROW(EXPECT_TRUE(copyProject(example, root / "copy", "copy", error)) << error);
  fs::permissions(root / "locked", fs::perms::owner_all);
  EXPECT_FALSE(fs::exists(fs::symlink_status(root / "copy" / "secret.txt")));
  fs::remove_all(root);
}
#endif
