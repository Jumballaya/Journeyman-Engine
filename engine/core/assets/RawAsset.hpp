#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

struct RawAsset {
  std::vector<uint8_t> data;
  std::filesystem::path filePath;
};