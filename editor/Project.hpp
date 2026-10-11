#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

using Json = nlohmann::ordered_json;  // keeps authored key order, so saves diff cleanly

// What a project file is, from its name.
enum class AssetKind { Folder, Scene, Prefab, Script, Image, Atlas, Tileset, Map, Ui, Style, Shader, Sound, Font, Input, Data, Other };

struct AssetKindInfo {
  const char* label;  // "Image"
  const char* icon;
};
AssetKind assetKindOf(const std::filesystem::path& relative);

AssetKindInfo assetKindInfo(AssetKind kind);
// The name scripts and titles use for a file: "coin" for assets/coin.prefab.json, "jump" for jump.wav.
std::string assetStem(const std::string& path);
// Whether `reference` ("assets/a.atlas.json#ship", "assets/b.png") suits a schema's suffix list.
bool assetMatches(std::string_view reference, const std::vector<std::string>& suffixes);

struct AssetFile {
  std::string path;  // project-relative, '/' separated
  AssetKind kind;
  std::filesystem::file_time_type modified;
  uintmax_t size = 0;
};

// A game project on disk: a folder with a .jm.json manifest. Paths given to
// and returned by a Project are relative to its folder.
class Project {
 public:
  // nullopt with `error` set if `folder` has no readable manifest.
  static std::optional<Project> open(const std::filesystem::path& folder, std::string& error);

  const std::filesystem::path& root() const { return _root; }
  std::filesystem::path abs(std::string_view relative) const { return _root / relative; }
  std::filesystem::path buildDir() const { return _root / "build"; }
  std::string name() const;

  Json& manifest() { return _manifest; }
  bool saveManifest(std::string& error);
  // Whether builds take the file: a scene in the manifest's scene list, or
  // anything else matched by its asset entries (globs as jm reads them).
  bool inBuild(const std::string& path) const;
  // Lists the file in the manifest unless it already is; true when it's in.
  bool addToBuild(const std::string& path, std::string& error);

  // Scenes listed in the manifest, then any other *.scene.json.
  std::vector<std::string> scenes() const;
  const std::vector<AssetFile>& files() const { return _files; }
  const AssetFile* file(std::string_view path) const;

  // Re-lists files when anything changed since the last scan (cheap to call
  // every second); true when the listing changed.
  bool rescan();
  // Newest source change, for deciding whether build/ is stale.
  std::filesystem::file_time_type newestSource() const { return _newest; }

  // Reads a project file's text; empty if missing.
  std::string readText(std::string_view path) const;
  bool writeText(std::string_view path, std::string_view text, std::string& error) const;

 private:
  std::filesystem::path _root;
  Json _manifest;
  std::vector<AssetFile> _files;
  std::filesystem::file_time_type _newest{};
};

// Whether a manifest's entries take `path` into builds (see Project::inBuild).
bool manifestTakes(const Json& manifest, const std::string& path);
// Whether one manifest asset entry (a path or a glob) matches `path`.
bool manifestEntryMatches(const std::string& entry, const std::string& path);

// Where new games go unless the person picks elsewhere: $JM_GAMES, else ~/Journeyman (as jm's).
std::filesystem::path gamesFolder();
// Copies the project in `from` to the new folder `to` as a game called `name`:
// its sources (not build/, dist/, .jm/, logs/, node_modules/, .git/), with .jm.json's name set
// and script libraries from outside it copied into libraries/ (@demos/common: libraries/demos-common).
bool copyProject(const std::filesystem::path& from, const std::filesystem::path& to, const std::string& name,
                 std::string& error);

// Recently opened projects, newest first, kept in the user's settings.
struct RecentProject {
  std::string path;
  std::string name;
  int64_t opened = 0;  // unix seconds
};
std::vector<RecentProject> recentProjects();
void rememberProject(const Project& project);
void forgetProject(const std::string& path);

// The scene last open in a project, remembered per user.
std::string lastScene(const Project& project);
void rememberScene(const Project& project, const std::string& scene);

// Where the Scene view looked at a scene last time (center x, y, zoom).
std::optional<std::array<float, 3>> sceneCamera(const Project& project, const std::string& scene);
void rememberSceneCamera(const Project& project, const std::string& scene, std::array<float, 3> camera);

// The editor's per-user settings folder (layout, recents).
std::filesystem::path settingsDir();

// The user's editor preferences (preferences.json), by key; "" when unset.
std::string preference(const std::string& key);
void setPreference(const std::string& key, const std::string& value);
