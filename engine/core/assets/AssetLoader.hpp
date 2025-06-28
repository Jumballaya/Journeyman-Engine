#pragma once

#include <filesystem>

#include "RawAsset.hpp"

class AssetLoader {
 public:
  static RawAsset loadRawBytes(const std::filesystem::path& filePath);
};