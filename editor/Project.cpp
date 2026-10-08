#include "Project.hpp"

#include "JsonFormat.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <regex>
#include <sstream>

#include "Entities.hpp"
#include "Icons.hpp"
#include "core/app/Platform.hpp"

namespace fs = std::filesystem;

namespace {

// Folders that hold output, tools or history rather than game content.
bool ignoredFolder(const std::string& name) {
  static const char* kIgnored[] = {"build", "build.next", "build.old", "dist", "logs", "tools", "tests"};
  return std::any_of(std::begin(kIgnored), std::end(kIgnored), [&](const char* n) { return name == n; });
}

}  // namespace

bool writeAtomically(const fs::path& target, std::string_view text, std::string& error) {
  const fs::path temp = target.string() + ".saving";
  std::error_code ec;
  fs::create_directories(target.parent_path(), ec);
  std::ofstream out(temp, std::ios::binary | std::ios::trunc);
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  out.close();
  if (out.fail()) error = "Couldn't write " + target.string();
  else if (fs::rename(temp, target, ec); ec) error = "Couldn't replace " + target.string() + ": " + ec.message();
  else return true;
  fs::remove(temp, ec);
  return false;
}

AssetKind assetKindOf(const fs::path& relative) {
  static const std::pair<std::string_view, AssetKind> kBySuffix[] = {
      {".scene.json", AssetKind::Scene}, {".prefab.json", AssetKind::Prefab}, {".atlas.json", AssetKind::Atlas},
      {".tsj", AssetKind::Tileset}, {".tmj", AssetKind::Map}, {".ui.html", AssetKind::Ui}, {".bindings.json", AssetKind::Input},
      {".ts", AssetKind::Script}, {".png", AssetKind::Image}, {".jpg", AssetKind::Image}, {".jpeg", AssetKind::Image},
      {".css", AssetKind::Style}, {".frag", AssetKind::Shader}, {".wav", AssetKind::Sound}, {".ogg", AssetKind::Sound},
      {".mp3", AssetKind::Sound}, {".flac", AssetKind::Sound}, {".ttf", AssetKind::Font}, {".otf", AssetKind::Font},
      {".json", AssetKind::Data}};
  const std::string name = relative.filename().string();
  for (const auto& [suffix, kind] : kBySuffix) {
    if (name.ends_with(suffix)) return kind;
  }
  return AssetKind::Other;
}

std::string assetStem(const std::string& path) {
  std::string name = fs::path(path).filename().string();
  for (std::string_view suffix : {".scene.json", ".prefab.json"}) {
    if (name.ends_with(suffix)) return name.substr(0, name.size() - suffix.size());
  }
  return fs::path(name).stem().string();
}

AssetKindInfo assetKindInfo(AssetKind kind) {
  switch (kind) {
    case AssetKind::Folder: return {"Folder", ICON_FOLDER_SIMPLE};
    case AssetKind::Scene: return {"Scene", ICON_FILM_SLATE};
    case AssetKind::Prefab: return {"Prefab", ICON_CUBE};
    case AssetKind::Script: return {"Script", ICON_FILE_TS};
    case AssetKind::Image: return {"Image", ICON_IMAGE};
    case AssetKind::Atlas: return {"Atlas", ICON_SQUARES_FOUR};
    case AssetKind::Tileset: return {"Tileset", ICON_GRID_FOUR};
    case AssetKind::Map: return {"Tile Map", ICON_MAP_TRIFOLD};
    case AssetKind::Ui: return {"UI", ICON_BROWSER};
    case AssetKind::Style: return {"Stylesheet", ICON_PAINT_BRUSH};
    case AssetKind::Shader: return {"Shader", ICON_SPARKLE};
    case AssetKind::Sound: return {"Sound", ICON_SPEAKER_HIGH};
    case AssetKind::Font: return {"Font", ICON_TEXT_AA};
    case AssetKind::Input: return {"Input Actions", ICON_GAME_CONTROLLER};
    case AssetKind::Data: return {"Data", ICON_BRACKETS_CURLY};
    case AssetKind::Other: break;
  }
  return {"File", ICON_FILE};
}

bool assetMatches(std::string_view reference, const std::vector<std::string>& suffixes) {
  if (suffixes.empty()) return true;
  const size_t hash = reference.find('#');
  for (const std::string& suffix : suffixes) {
    if (suffix.ends_with('#')) {
      const std::string_view file = std::string_view(suffix).substr(0, suffix.size() - 1);
      if (hash != std::string_view::npos && reference.substr(0, hash).ends_with(file)) return true;
      continue;
    }
    if (hash == std::string_view::npos && reference.ends_with(suffix)) return true;
  }
  return false;
}

std::optional<Project> Project::open(const fs::path& folder, std::string& error) {
  Project project;
  std::error_code ec;
  project._root = fs::canonical(folder, ec);
  if (ec) {
    error = "Folder not found: " + folder.string();
    return std::nullopt;
  }
  const std::string text = project.readText(".jm.json");
  if (text.empty()) {
    error = "No .jm.json in " + project._root.string() + ". Is it a Journeyman project?";
    return std::nullopt;
  }
  try {
    project._manifest = Json::parse(text);
  } catch (const std::exception& e) {
    error = std::string(".jm.json is not valid JSON: ") + e.what();
    return std::nullopt;
  }
  if (!project._manifest.is_object()) {
    error = ".jm.json should hold an object ({\"name\": ...}).";
    return std::nullopt;
  }
  project.rescan();
  return project;
}

std::string Project::name() const {
  const std::string name = _manifest.value("name", std::string());
  return name.empty() ? _root.filename().string() : name;
}

bool Project::saveManifest(std::string& error) {
  wholeNumbersAsIntegers(_manifest);
  return writeText(".jm.json", formatJson(_manifest), error);
}

// A manifest asset entry as jm matches it: "*" within one path segment, "**"
// across segments, "?" one character; plain entries match themselves.
bool manifestEntryMatches(const std::string& pattern, const std::string& path) {
  if (pattern.find_first_of("*?") == std::string::npos) return pattern == path;
  std::string re = "^";
  for (size_t i = 0; i < pattern.size(); ++i) {
    if (pattern.compare(i, 3, "**/") == 0) {
      re += "(?:.*/)?";
      i += 2;
    } else if (pattern.compare(i, 2, "**") == 0) {
      re += ".*";
      ++i;
    } else if (pattern[i] == '*') {
      re += "[^/]*";
    } else if (pattern[i] == '?') {
      re += "[^/]";
    } else {
      if (std::string("\\^$.|+()[]{}").find(pattern[i]) != std::string::npos) re += '\\';
      re += pattern[i];
    }
  }
  return std::regex_match(path, std::regex(re + "$"));
}

bool manifestTakes(const Json& manifest, const std::string& path) {
  const bool scene = assetKindOf(path) == AssetKind::Scene;
  for (const Json& entry : manifest.value(scene ? "scenes" : "assets", Json::array())) {
    if (entry.is_string() && manifestEntryMatches(entry.get<std::string>(), path)) return true;
  }
  return false;
}

bool Project::inBuild(const std::string& path) const { return manifestTakes(_manifest, path); }

bool Project::addToBuild(const std::string& path, std::string& error) {
  if (inBuild(path) || path.find("node_modules") != std::string::npos || path.starts_with("build/")) return true;
  Json& list = _manifest[assetKindOf(path) == AssetKind::Scene ? "scenes" : "assets"];
  if (!list.is_array()) list = Json::array();
  list.push_back(path);
  return saveManifest(error);
}

std::vector<std::string> Project::scenes() const {
  std::vector<std::string> out;
  for (const auto& s : _manifest.value("scenes", Json::array())) {
    if (s.is_string()) out.push_back(s.get<std::string>());
  }
  for (const AssetFile& f : _files) {
    if (f.kind == AssetKind::Scene && std::find(out.begin(), out.end(), f.path) == out.end()) out.push_back(f.path);
  }
  return out;
}

const AssetFile* Project::file(std::string_view path) const {
  auto it = std::find_if(_files.begin(), _files.end(), [&](const AssetFile& f) { return f.path == path; });
  return it == _files.end() ? nullptr : &*it;
}

bool Project::rescan() {
  std::vector<AssetFile> files;
  fs::file_time_type newest{};
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(_root, fs::directory_options::skip_permission_denied, ec);
       it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) break;
    const fs::path relative = fs::relative(it->path(), _root, ec);
    const std::string name = it->path().filename().string();
    if (it->is_directory(ec)) {
      // Hidden and dependency folders at any depth; output and tool folders at the top.
      if (name.starts_with('.') || name == "node_modules" || (it.depth() == 0 && ignoredFolder(name))) {
        it.disable_recursion_pending();
      } else {
        files.push_back({relative.generic_string(), AssetKind::Folder, it->last_write_time(ec), 0});
      }
      continue;
    }
    if (name.starts_with('.') && name != ".jm.json") continue;
    AssetFile file{relative.generic_string(), assetKindOf(relative), it->last_write_time(ec), it->file_size(ec)};
    newest = std::max(newest, file.modified);
    files.push_back(std::move(file));
  }
  std::sort(files.begin(), files.end(), [](const AssetFile& a, const AssetFile& b) { return a.path < b.path; });
  const bool changed = files.size() != _files.size() ||
                       !std::equal(files.begin(), files.end(), _files.begin(), [](const AssetFile& a, const AssetFile& b) {
                         return a.path == b.path && a.modified == b.modified && a.size == b.size;
                       });
  _files = std::move(files);
  _newest = newest;
  return changed;
}

std::string Project::readText(std::string_view path) const {
  std::ifstream in(_root / path, std::ios::binary);
  if (!in) return {};
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Project::writeText(std::string_view path, std::string_view text, std::string& error) const {
  return writeAtomically(_root / path, text, error);
}

fs::path settingsDir() {
  const fs::path dir = platform::userDataDir("Journeyman Editor");
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

namespace {

Json readSetting(const char* file) {
  std::ifstream in(settingsDir() / file);
  return Json::parse(in, nullptr, false);
}

void writeSetting(const char* file, const Json& value) {
  std::string error;
  writeAtomically(settingsDir() / file, value.dump(2), error);
}

}  // namespace

std::string preference(const std::string& key) {
  const Json prefs = readSetting("preferences.json");
  return prefs.is_object() && prefs.value(key, Json()).is_string() ? prefs[key].get<std::string>() : std::string();
}

void setPreference(const std::string& key, const std::string& value) {
  Json prefs = readSetting("preferences.json");
  if (!prefs.is_object()) prefs = Json::object();
  prefs[key] = value;
  writeSetting("preferences.json", prefs);
}

namespace {

void saveRecents(const std::vector<RecentProject>& recents) {
  Json list = Json::array();
  for (const auto& r : recents) list.push_back({{"path", r.path}, {"name", r.name}, {"opened", r.opened}});
  writeSetting("recent.json", list);
}

// This project's entry in projects.json (an object, maybe empty).
Json projectState(const Project& project) {
  const Json all = readSetting("projects.json");
  const auto it = all.find(project.root().string());
  return it != all.end() && it->is_object() ? *it : Json::object();
}

void updateProjectState(const Project& project, const std::function<void(Json&)>& change) {
  Json all = readSetting("projects.json");
  if (!all.is_object()) all = Json::object();
  Json& state = all[project.root().string()];
  if (!state.is_object()) state = Json::object();
  change(state);
  writeSetting("projects.json", all);
}

}  // namespace

std::vector<RecentProject> recentProjects() {
  std::vector<RecentProject> out;
  const Json list = readSetting("recent.json");
  if (!list.is_array()) return out;
  for (const auto& r : list) {
    if (!r.is_object()) continue;
    out.push_back({r.value("path", std::string()), r.value("name", std::string()), r.value("opened", int64_t{0})});
  }
  return out;
}

void rememberProject(const Project& project) {
  auto recents = recentProjects();
  const std::string path = project.root().string();
  std::erase_if(recents, [&](const RecentProject& r) { return r.path == path; });
  const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch());
  recents.insert(recents.begin(), {path, project.name(), now.count()});
  if (recents.size() > 12) recents.resize(12);
  saveRecents(recents);
}

void forgetProject(const std::string& path) {
  auto recents = recentProjects();
  std::erase_if(recents, [&](const RecentProject& r) { return r.path == path; });
  saveRecents(recents);
}

std::string lastScene(const Project& project) { return projectState(project).value("scene", std::string()); }

void rememberScene(const Project& project, const std::string& scene) {
  updateProjectState(project, [&](Json& state) { state["scene"] = scene; });
}

std::optional<std::array<float, 3>> sceneCamera(const Project& project, const std::string& scene) {
  const Json camera = projectState(project).value("cameras", Json::object()).value(scene, Json());
  const auto number = [](const Json& v) { return v.is_number(); };
  if (!camera.is_array() || camera.size() != 3 || !std::all_of(camera.begin(), camera.end(), number)) return std::nullopt;
  return std::array<float, 3>{camera[0].get<float>(), camera[1].get<float>(), camera[2].get<float>()};
}

void rememberSceneCamera(const Project& project, const std::string& scene, std::array<float, 3> camera) {
  updateProjectState(project, [&](Json& state) { state["cameras"][scene] = {camera[0], camera[1], camera[2]}; });
}
