#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "../assets/TempDir.hpp"
#include "Platform.hpp"

namespace fs = std::filesystem;

namespace {
std::string readAll(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}
size_t entries(const fs::path& dir) { return std::distance(fs::directory_iterator(dir), fs::directory_iterator()); }
}  // namespace

TEST(WriteAtomically, ReplacesTheFileAndLeavesNoTemporary) {
  TempDir dir;
  const auto scene = dir.path() / "level.scene.json";
  std::ofstream(scene) << "old";
  std::string error;
  ASSERT_TRUE(platform::writeAtomically(scene, "new", error)) << error;
  EXPECT_EQ(readAll(scene), "new");
  EXPECT_EQ(entries(dir.path()), 1u);
}

// A hard link sees in-place writes; it keeps the old bytes only when the file
// is replaced whole, so there's no moment with a truncated file.
TEST(WriteAtomically, NeverRewritesTheFileInPlace) {
  TempDir dir;
  const auto save = dir.path() / "save.json";
  std::ofstream(save) << R"({"hiscore": 1})";
  std::error_code ec;
  fs::create_hard_link(save, dir.path() / "link.json", ec);
  if (ec) GTEST_SKIP() << "no hard links here: " << ec.message();
  std::string error;
  ASSERT_TRUE(platform::writeAtomically(save, R"({"hiscore": 2})", error)) << error;
  EXPECT_EQ(readAll(save), R"({"hiscore": 2})");
  EXPECT_EQ(readAll(dir.path() / "link.json"), R"({"hiscore": 1})");
}

TEST(WriteAtomically, AFailedReplaceSaysWhyAndLeavesNoTemporary) {
  TempDir dir;
  fs::create_directories(dir.path() / "taken" / "inside");  // a non-empty folder can't be replaced by a file
  std::string error;
  EXPECT_FALSE(platform::writeAtomically(dir.path() / "taken", "data", error));
  EXPECT_NE(error.find("taken"), std::string::npos) << error;
  EXPECT_EQ(entries(dir.path()), 1u);
}

// A bare name has no folder to make (MSVC fails create_directories("")): still written.
TEST(WriteAtomically, WritesABareFileNameInTheCurrentFolder) {
  TempDir dir;
  const fs::path was = fs::current_path();
  fs::current_path(dir.path());
  std::string error;
  const bool ok = platform::writeAtomically("frame.png", "png", error);
  fs::current_path(was);
  EXPECT_TRUE(ok) << error;
  EXPECT_EQ(readAll(dir.path() / "frame.png"), "png");
}
