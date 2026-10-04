#pragma once

#include <cstdint>
#include <string_view>

// Decodes the next UTF-8 code point at s[i] and advances i. Malformed bytes
// decode as U+FFFD so text never stalls.
inline uint32_t nextCodepoint(std::string_view s, size_t& i) {
  const auto byte = [&](size_t k) { return static_cast<uint8_t>(s[k]); };
  const uint8_t c = byte(i);
  int extra = c < 0x80 ? 0 : (c >> 5) == 0x6 ? 1 : (c >> 4) == 0xE ? 2 : (c >> 3) == 0x1E ? 3 : -1;
  if (extra < 0 || i + extra >= s.size() + (extra == 0 ? 1 : 0)) {
    ++i;
    return 0xFFFD;
  }
  uint32_t cp = extra == 0 ? c : c & (0x3F >> extra);
  for (int k = 1; k <= extra; ++k) {
    if ((byte(i + k) & 0xC0) != 0x80) {
      ++i;
      return 0xFFFD;
    }
    cp = (cp << 6) | (byte(i + k) & 0x3F);
  }
  i += 1 + extra;
  return cp;
}
