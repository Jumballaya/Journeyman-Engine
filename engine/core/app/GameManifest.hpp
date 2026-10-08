#pragma once

#include <algorithm>

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

// The game's .jm.json: identity, scenes, preloaded assets and module config.
struct GameManifest {
  std::string name = "Unnamed Game";
  std::string version = "0.0.1";
  std::string entryScene;
  std::vector<std::string> scenes;
  std::vector<std::string> assets;
  nlohmann::json config;

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

  // For a short name that resolve() didn't find: the listed name + suffix
  // closest to it ("brick" for "brik"), or "" if none is close.
  std::string closest(std::string_view name, std::string_view suffix) const {
    std::string best;
    size_t bestDistance = std::max<size_t>(2, name.size() / 3) + 1;
    for (const auto* list : {&scenes, &assets}) {
      for (const std::string& path : *list) {
        if (!path.ends_with(suffix)) continue;
        std::string_view candidate(path);
        candidate.remove_suffix(suffix.size());
        candidate.remove_prefix(candidate.rfind('/') + 1);  // npos + 1 is 0
        if (const size_t d = editDistance(name, candidate); d < bestDistance) {
          bestDistance = d;
          best = std::string(candidate);
        }
      }
    }
    return best;
  }

 private:
  static size_t editDistance(std::string_view a, std::string_view b) {
    std::vector<size_t> row(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) row[j] = j;
    for (size_t i = 1; i <= a.size(); ++i) {
      size_t diagonal = row[0];
      row[0] = i;
      for (size_t j = 1; j <= b.size(); ++j) {
        const size_t up = row[j];
        row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
        diagonal = up;
      }
    }
    return row[b.size()];
  }
};
