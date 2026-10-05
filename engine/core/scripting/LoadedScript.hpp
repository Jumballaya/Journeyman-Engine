#pragma once

#include <cstdint>
#include <string>
#include <vector>

// A script's wasm bytes. Not a parsed module: wasm3 binds a module to one runtime,
// so every instance parses its own.
struct LoadedScript {
  std::string path;  // for logs
  std::vector<uint8_t> binary;
};
