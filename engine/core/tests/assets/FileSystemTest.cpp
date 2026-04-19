#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
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
