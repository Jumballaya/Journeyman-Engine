#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

// An ordered list, so components apply in the same (alphabetical, from the
// JSON) order on every instantiation.
struct Prefab {
  std::vector<std::pair<std::string, nlohmann::json>> components;
  std::vector<std::string> tags;
  // Entities that come with it, attached to it: scene entries ({"name",
  // "components"} or {"name", "prefab", "overrides"}), each with its own "children".
  nlohmann::json children = nlohmann::json::array();
};
