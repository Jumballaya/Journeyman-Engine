#include "PrefabLoader.hpp"

#include <string>

Prefab PrefabLoader::loadFromJson(const nlohmann::json &json) {
  Prefab prefab;
  if (auto it = json.find("components"); it != json.end() && it->is_object()) {
    for (const auto &[name, data] : it->items()) prefab.components.emplace_back(name, data);
  }
  if (auto it = json.find("tags"); it != json.end() && it->is_array()) {
    for (const auto &tag : *it) {
      if (tag.is_string()) prefab.tags.push_back(tag.get<std::string>());
    }
  }
  return prefab;
}

Prefab PrefabLoader::loadFromBytes(std::span<const uint8_t> bytes) {
  return loadFromJson(nlohmann::json::parse(bytes.begin(), bytes.end()));
}
