#pragma once
#include <wasm3.h>

#include <cstdint>
#include <vector>

struct LoadedScript {
  IM3Module module = nullptr;
  IM3Function onUpdate = nullptr;
  std::vector<uint8_t> binary;
};