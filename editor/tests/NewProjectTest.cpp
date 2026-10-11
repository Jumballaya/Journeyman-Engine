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
