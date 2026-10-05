#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "Thumbnails.hpp"

class HostedEngine;
class Project;

// Pictures of UI screens as the game draws them, for the Assets panel and the
// Inspector: drawn once by a hidden engine, kept until the file or the build
// changes. At most one is drawn per frame, so a folder of screens fills in.
class UiThumbnails {
 public:
  static UiThumbnails& instance();
  std::optional<Thumbnails::Picture> get(const Project& project, const std::string& path, uint64_t buildGeneration);
  void clear();

 private:
  struct Shot {
    unsigned texture = 0;
    ImVec2 size;
    std::filesystem::file_time_type modified;
    uint64_t build = 0;
  };
  std::unique_ptr<HostedEngine> _engine;
  std::filesystem::path _engineRoot;
  uint64_t _engineBuild = ~0ull;
  std::map<std::string, Shot> _shots;
  int _lastDrawFrame = -1;

  bool draw(const Project& project, const std::string& path, Shot& shot);
};
