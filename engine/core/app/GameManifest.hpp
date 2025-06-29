#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct GameManifest {
  std::string name = "Unnamed Game";
  std::string version = "0.0.1";
  std::string entryScene;

  std::vector<std::string> assets;
  std::vector<std::string> scripts;

  nlohmann::json config;
};