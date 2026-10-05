#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

class Project;

// Pictures of project images and atlas regions for the editor's own UI
// (asset tiles, sprite fields, drop previews). Loaded on first use, kept until
// the file changes.
class Thumbnails {
 public:
  struct Picture {
    ImTextureID texture;
    ImVec2 uv0, uv1;  // ImGui order: top-left, bottom-right
    ImVec2 size;      // pixels of the image or region
  };

  // "assets/a.png" or "assets/x.atlas.json#region" (atlases read from build/).
  std::optional<Picture> get(const Project& project, const std::string& reference);
  // An atlas's region names, sorted; empty before the project is built.
  std::vector<std::string> regions(const Project& project, const std::string& atlas);
  void clear();

  static Thumbnails& instance();

 private:
  struct Texture {
    unsigned id = 0;
    int width = 0, height = 0;
    std::filesystem::file_time_type modified;
  };
  struct Atlas {
    std::string image;  // build-relative
    std::map<std::string, std::array<int, 4>> regions;
    std::filesystem::file_time_type modified;
  };
  std::map<std::string, Texture> _textures;  // by absolute path
  std::map<std::string, Atlas> _atlases;     // by project path

  const Texture* texture(const std::filesystem::path& file);
  const Atlas* atlas(const Project& project, const std::string& path);
};
