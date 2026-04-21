#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

// Components are an ordered list (not a map) so the apply order at
// instantiation is deterministic. PrefabLoader populates by iterating
// nlohmann::json's object (alphabetical), which gives both scene-load paths the
// same iteration order.
struct Prefab {
  std::vector<std::pair<std::string, nlohmann::json>> components;
  std::vector<std::string> tags;
};
