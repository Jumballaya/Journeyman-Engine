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

TEST(WriteAtomically, KeepsASymlinkAndTheFilesMode) {
  TempDir dir;
  const auto shared = dir.path() / "shared.json", link = dir.path() / "level.scene.json";
  std::ofstream(shared) << "old";
  std::error_code ec;
  fs::create_symlink(shared, link, ec);
  if (ec) GTEST_SKIP() << "no symlinks here: " << ec.message();
  fs::permissions(shared, fs::perms::owner_read | fs::perms::owner_write);
  const auto before = fs::status(shared).permissions();  // Windows reports perms::all, not the 0600 asked for
  std::string error;
  ASSERT_TRUE(platform::writeAtomically(link, "new", error)) << error;
  EXPECT_TRUE(fs::is_symlink(link));
  EXPECT_EQ(readAll(shared), "new");
  EXPECT_EQ(fs::status(shared).permissions(), before);
}

TEST(WriteAtomically, CreatesADanglingSymlinksTarget) {
  TempDir dir;
  const auto link = dir.path() / "level.scene.json";
  std::error_code ec;
  fs::create_symlink("shared.json", link, ec);
  if (ec) GTEST_SKIP() << "no symlinks here: " << ec.message();
  std::string error;
  ASSERT_TRUE(platform::writeAtomically(link, "new", error)) << error;
  EXPECT_TRUE(fs::is_symlink(link));
  EXPECT_EQ(readAll(dir.path() / "shared.json"), "new");
}

namespace {
// The write must land where reading through link does: POSIX resolves alias
// before "..", Windows drops "alias\.." first, so either file may be it.
void expectWritesWhereReads(const fs::path& dir, const fs::path& link) {
  std::ofstream(dir / "real" / "shared.json") << "old";
  std::ofstream(dir / "shared.json") << "old";
  std::string error;
  ASSERT_TRUE(platform::writeAtomically(link, "new", error)) << error;
  EXPECT_EQ(readAll(link), "new");
  const auto both = readAll(dir / "real" / "shared.json") + readAll(dir / "shared.json");
  EXPECT_TRUE(both == "newold" || both == "oldnew") << both;
}

// Makes dir/real/nested and the symlink dir/alias to it; false if symlinks are unavailable.
bool makeLinkedFolder(const fs::path& dir) {
  std::error_code ec;
  fs::create_directories(dir / "real" / "nested");
  fs::create_directory_symlink(dir / "real" / "nested", dir / "alias", ec);
  return !ec;
}
}  // namespace

TEST(WriteAtomically, ARelativeSymlinkInALinkedFolderIsWrittenWhereItReads) {
  TempDir dir;
  if (!makeLinkedFolder(dir.path())) GTEST_SKIP() << "no symlinks here";
  fs::create_symlink(fs::path("..") / "shared.json", dir.path() / "real" / "nested" / "scene.json");
  expectWritesWhereReads(dir.path(), dir.path() / "alias" / "scene.json");
}

TEST(WriteAtomically, ASymlinkThroughALinkedFoldersParentIsWrittenWhereItReads) {
  TempDir dir;
  if (!makeLinkedFolder(dir.path())) GTEST_SKIP() << "no symlinks here";
  fs::create_symlink(fs::path("alias") / ".." / "shared.json", dir.path() / "scene.json");
  expectWritesWhereReads(dir.path(), dir.path() / "scene.json");
}

TEST(WriteAtomically, ABareRelativeSymlinkInALinkedFolderIsWrittenWhereItReads) {
  TempDir dir;
  if (!makeLinkedFolder(dir.path())) GTEST_SKIP() << "no symlinks here";
  fs::create_symlink(fs::path("..") / "shared.json", dir.path() / "real" / "nested" / "scene.json");
  const auto cwd = fs::current_path();
  fs::current_path(dir.path() / "alias");
  expectWritesWhereReads(dir.path(), "scene.json");
  fs::current_path(cwd);
}

TEST(WriteAtomically, ASymlinkLoopIsAnErrorAndKeepsTheLinks) {
  TempDir dir;
  std::error_code ec;
  fs::create_symlink("b", dir.path() / "a", ec);
  if (ec) GTEST_SKIP() << "no symlinks here: " << ec.message();
  fs::create_symlink("a", dir.path() / "b");
  std::string error;
  EXPECT_FALSE(platform::writeAtomically(dir.path() / "a", "new", error));
  EXPECT_FALSE(error.empty());
  EXPECT_TRUE(fs::is_symlink(dir.path() / "a"));
  EXPECT_TRUE(fs::is_symlink(dir.path() / "b"));
}
