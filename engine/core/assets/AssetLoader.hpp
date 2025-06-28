#pragma once

#include <filesystem>

#include "FileSystem.hpp"
#include "RawAsset.hpp"

class AssetLoader {
 public:
  static RawAsset loadRawBytes(const std::filesystem::path& filePath);

  static void setFileSystem(FileSystem* fs);

 private:
  static inline FileSystem* _fileSystem = nullptr;
};