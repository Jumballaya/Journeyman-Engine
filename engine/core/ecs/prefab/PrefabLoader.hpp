#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <span>

#include "Prefab.hpp"

class PrefabLoader {
public:
  static Prefab loadFromJson(const nlohmann::json &json);
  static Prefab loadFromBytes(std::span<const uint8_t> bytes);
};
