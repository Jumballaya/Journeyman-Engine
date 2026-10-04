#pragma once

#include <wasm3.h>

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

// Bounds-checked access to a script's linear memory from inside a host
// function (`runtime` is the IM3Runtime that m3ApiRawFunction provides).
// Every helper treats an out-of-range (ptr, len) as "no data" instead of
// reading past the end of wasm memory.
namespace wasm_memory {

// Returns the UTF-8 string at [ptr, ptr + len), or nullopt if out of range.
inline std::optional<std::string> readString(IM3Runtime runtime, int32_t ptr, int32_t len) {
  uint32_t memSize = 0;
  uint8_t* memory = m3_GetMemory(runtime, &memSize, 0);
  if (!memory || ptr < 0 || len < 0 ||
      static_cast<uint64_t>(ptr) + static_cast<uint64_t>(len) > memSize) {
    return std::nullopt;
  }
  return std::string(reinterpret_cast<const char*>(memory + ptr), static_cast<size_t>(len));
}

// Copies `value` into [ptr, ptr + capacity). Returns the full byte length of
// `value` (which may exceed capacity — callers grow their buffer and retry),
// or -1 if the destination range is invalid.
inline int32_t writeString(IM3Runtime runtime, int32_t ptr, int32_t capacity, std::string_view value) {
  uint32_t memSize = 0;
  uint8_t* memory = m3_GetMemory(runtime, &memSize, 0);
  if (!memory || ptr < 0 || capacity < 0 ||
      static_cast<uint64_t>(ptr) + static_cast<uint64_t>(capacity) > memSize) {
    return -1;
  }
  const size_t n = std::min(value.size(), static_cast<size_t>(capacity));
  std::memcpy(memory + ptr, value.data(), n);
  return static_cast<int32_t>(value.size());
}

// Returns a writable pointer to [ptr, ptr + len), or nullptr if out of range.
inline uint8_t* span(IM3Runtime runtime, int32_t ptr, int32_t len) {
  uint32_t memSize = 0;
  uint8_t* memory = m3_GetMemory(runtime, &memSize, 0);
  if (!memory || ptr < 0 || len < 0 ||
      static_cast<uint64_t>(ptr) + static_cast<uint64_t>(len) > memSize) {
    return nullptr;
  }
  return memory + ptr;
}

}  // namespace wasm_memory
