#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

class FileSystem {
 public:
  FileSystem() = default;
  ~FileSystem() = default;

  bool exists(const std::filesystem::path& filePath) const;
  std::vector<uint8_t> read(const std::filesystem::path& filePath) const;

  void mountFolder(const std::filesystem::path& folderPath);    // Starting from a folder
  void mountArchive(const std::filesystem::path& archivePath);  // Starting from a zip file

 private:
  std::filesystem::path _mountedFolder = ".";
};