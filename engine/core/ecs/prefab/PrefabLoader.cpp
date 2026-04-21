#include "PrefabLoader.hpp"

#include <string>

Prefab PrefabLoader::loadFromJson(const nlohmann::json &json) {
  Prefab prefab;

  if (json.contains("components") && json["components"].is_object()) {
    for (auto it = json["components"].begin(); it != json["components"].end();
         ++it) {
      prefab.components.emplace_back(it.key(), it.value());
    }
  }

  if (json.contains("tags") && json["tags"].is_array()) {
    for (const auto &tag : json["tags"]) {
      if (tag.is_string()) {
        prefab.tags.push_back(tag.get<std::string>());
      }
    }
  }

  return prefab;
}

Prefab PrefabLoader::loadFromBytes(std::span<const uint8_t> bytes) {
  std::string str(reinterpret_cast<const char *>(bytes.data()), bytes.size());
  nlohmann::json json = nlohmann::json::parse(str);
  return loadFromJson(json);
}
