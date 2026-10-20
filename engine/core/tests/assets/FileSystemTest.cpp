#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "FileSystem.hpp"
#include "TempDir.hpp"

// read() returns the file's bytes verbatim, including zero bytes and
// non-ASCII values. Pins the binary-safe read contract.
TEST(FileSystem, ReadReturnsFileBytesExactly) {
  TempDir dir;
  std::vector<uint8_t> bytes = {0x00, 0xFF, 0x41, 0x42, 0x00, 0x7F, 0x80};
  dir.writeFile("bin.dat", bytes);

  FileSystem fs;
  fs.mountFolder(dir.path());
  auto got = fs.read("bin.dat");

  EXPECT_EQ(got, bytes);
}

// read() on a path that does not exist throws rather than returning empty.
TEST(FileSystem, ReadThrowsOnMissingFile) {
  TempDir dir;
  FileSystem fs;
  fs.mountFolder(dir.path());
  EXPECT_THROW(fs.read("does-not-exist.txt"), std::runtime_error);
}

// exists() returns true for files present under the mounted folder and false
// for files that don't exist.
TEST(FileSystem, ExistsReflectsFilesystemState) {
  TempDir dir;
  dir.writeFile("present.txt", "hi");

  FileSystem fs;
  fs.mountFolder(dir.path());
  EXPECT_TRUE(fs.exists("present.txt"));
  EXPECT_FALSE(fs.exists("missing.txt"));
}

TEST(FileSystem, TryReadReturnsBytesForReadableFiles) {
  TempDir dir;
  const std::vector<uint8_t> bytes = {0x00, 0xFF, 0x41};
  dir.writeFile("bin.dat", bytes);
  dir.writeFile("empty.dat", "");
  FileSystem fs;
  fs.mountFolder(dir.path());

  const auto read = fs.tryRead("bin.dat");
  ASSERT_TRUE(read);
  EXPECT_EQ(*read, bytes);
  const auto empty = fs.tryRead("empty.dat");
  ASSERT_TRUE(empty);
  EXPECT_TRUE(empty->empty());
}

TEST(FileSystem, TryReadReturnsNoBytesForMissingFilesAndDirectories) {
  TempDir dir;
  FileSystem fs;
  fs.mountFolder(dir.path());

  EXPECT_FALSE(fs.tryRead("missing.dat"));
  EXPECT_FALSE(fs.tryRead("."));
}

TEST(FileSystem, TryReadReturnsNoBytesForInvalidPath) {
  TempDir dir;
  FileSystem fs;
  fs.mountFolder(dir.path());

  // A single component beyond filesystem limits makes the status lookup fail.
  EXPECT_FALSE(fs.tryRead(std::string(8192, 'x')));
}
