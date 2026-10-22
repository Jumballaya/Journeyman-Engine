#include "Color.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

std::optional<glm::vec4> hexColor(std::string_view text) {
  if (text.empty() || text[0] != '#') return std::nullopt;
  std::string hex(text.substr(1));
  if (hex.size() == 3 || hex.size() == 4) {
    std::string expanded;
    for (char c : hex) expanded += std::string(2, c);
    hex = expanded;
  }
  if (hex.size() != 6 && hex.size() != 8) return std::nullopt;
  if (!std::all_of(hex.begin(), hex.end(), [](unsigned char c) { return std::isxdigit(c); })) return std::nullopt;
  auto byte = [&](size_t at) { return std::strtoul(hex.substr(at, 2).c_str(), nullptr, 16) / 255.0f; };
  return glm::vec4(byte(0), byte(2), byte(4), hex.size() == 8 ? byte(6) : 1.0f);
}

std::optional<glm::vec4> colorOf(const nlohmann::json& value) {
  if (value.is_string()) return hexColor(value.get<std::string>());
  if (!value.is_array() || (value.size() != 3 && value.size() != 4)) return std::nullopt;
  if (!std::all_of(value.begin(), value.end(), [](const nlohmann::json& n) { return n.is_number(); })) return std::nullopt;
  return glm::vec4(value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value.size() == 4 ? value[3].get<float>() : 1.0f);
}
