#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Storage for a parsed script asset's source bytes + import list.
//
// The wasm3 parsed module (IM3Module) is intentionally NOT cached here:
// `m3_LoadModule` transfers module ownership to a runtime, so a single
// parsed module can only ever be bound to one IM3Runtime. To support N
// concurrent script instances of the same script, ScriptManager parses a
// FRESH module per instance from `binary`. LoadedScript only holds the
// reusable inputs (raw bytes + imports list).
struct LoadedScript {
  std::vector<uint8_t> binary;
  std::vector<std::string> imports;
};
