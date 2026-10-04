#pragma once

#include <cstdint>
#include <vector>

// A script's wasm bytes. Not a parsed module: wasm3 binds a module to one runtime,
// so every instance parses its own.
struct LoadedScript {
  std::vector<uint8_t> binary;
};
