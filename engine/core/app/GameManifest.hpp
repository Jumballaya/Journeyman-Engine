#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

// The game's .jm.json: identity, scenes, preloaded assets, module config and
// multiplayer settings.
struct GameManifest {
  std::string name = "Unnamed Game";
  std::string version = "0.0.1";
  std::string entryScene;
  std::vector<std::string> scenes;
  std::vector<std::string> assets;
  nlohmann::json config;
  nlohmann::json net = nlohmann::json::object();  // multiplayer settings (engine/net)

  // Paths pass through; a short name ("bullet") becomes the listed scene or
  // asset named name + suffix ("assets/prefabs/bullet.prefab.json"), if any.
  std::string resolve(std::string_view name, std::string_view suffix) const {
    if (name.find('/') != std::string_view::npos || name.ends_with(suffix)) return std::string(name);
    const std::string file = std::string(name) + std::string(suffix);
    for (const auto* list : {&scenes, &assets}) {
      for (const std::string& path : *list) {
        if (path == file || path.ends_with("/" + file)) return path;
      }
    }
    return std::string(name);
  }
};
