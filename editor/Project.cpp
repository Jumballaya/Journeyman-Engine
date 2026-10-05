#include "Project.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

#include "Icons.hpp"
#include "core/app/Platform.hpp"

namespace fs = std::filesystem;

namespace {

bool endsWith(std::string_view s, std::string_view suffix) {
  return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

// Folders that hold output, tools or history rather than game content.
bool ignoredFolder(const std::string& name) {
  static const char* kIgnored[] = {"build", "dist", "logs", "node_modules", "tools", "tests"};
  return name.starts_with('.') || std::any_of(std::begin(kIgnored), std::end(kIgnored), [&](const char* n) { return name == n; });
}

}  // namespace

AssetKind assetKindOf(const fs::path& relative) {
  const std::string name = relative.filename().string();
  if (endsWith(name, ".scene.json")) return AssetKind::Scene;
  if (endsWith(name, ".prefab.json")) return AssetKind::Prefab;
  if (endsWith(name, ".atlas.json")) return AssetKind::Atlas;
  if (endsWith(name, ".tileset.json")) return AssetKind::Tileset;
  if (endsWith(name, ".ui.html")) return AssetKind::Ui;
  const std::string ext = relative.extension().string();
  if (ext == ".ts") return AssetKind::Script;
  if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") return AssetKind::Image;
  if (ext == ".txt" && relative.generic_string().find("maps/") != std::string::npos) return AssetKind::Map;
  if (ext == ".css") return AssetKind::Style;
  if (ext == ".frag") return AssetKind::Shader;
  if (ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac") return AssetKind::Sound;
  if (ext == ".ttf" || ext == ".otf") return AssetKind::Font;
  if (ext == ".json") return AssetKind::Data;
  return AssetKind::Other;
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
    case AssetKind::Map: return {"Map", ICON_MAP_TRIFOLD};
    case AssetKind::Ui: return {"UI", ICON_BROWSER};
    case AssetKind::Style: return {"Stylesheet", ICON_PAINT_BRUSH};
    case AssetKind::Shader: return {"Shader", ICON_SPARKLE};
    case AssetKind::Sound: return {"Sound", ICON_SPEAKER_HIGH};
    case AssetKind::Font: return {"Font", ICON_TEXT_AA};
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
      if (hash != std::string_view::npos && endsWith(reference.substr(0, hash), file)) return true;
      continue;
    }
    if (hash == std::string_view::npos && endsWith(reference, suffix)) return true;
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
  project.rescan();
  return project;
}

std::string Project::name() const {
  const std::string name = _manifest.value("name", std::string());
  return name.empty() ? _root.filename().string() : name;
}

bool Project::saveManifest(std::string& error) { return writeText(".jm.json", _manifest.dump(2) + "\n", error); }

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
      if (it.depth() == 0 && ignoredFolder(name)) it.disable_recursion_pending();
      else if (name.starts_with('.')) it.disable_recursion_pending();
      else files.push_back({relative.generic_string(), AssetKind::Folder, it->last_write_time(ec), 0});
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
  // Write beside, then rename: a crash mid-save never leaves half a file.
  const fs::path target = _root / path;
  const fs::path temp = target.string() + ".saving";
  std::error_code ec;
  fs::create_directories(target.parent_path(), ec);
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out || !out.write(text.data(), static_cast<std::streamsize>(text.size()))) {
      error = "Couldn't write " + target.string();
      return false;
    }
  }
  fs::rename(temp, target, ec);
  if (ec) {
    error = "Couldn't replace " + target.string() + ": " + ec.message();
    fs::remove(temp, ec);
    return false;
  }
  return true;
}

fs::path settingsDir() {
  const fs::path dir = platform::userDataDir("Journeyman Editor");
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

namespace {

fs::path recentFile() { return settingsDir() / "recent.json"; }

void saveRecents(const std::vector<RecentProject>& recents) {
  Json list = Json::array();
  for (const auto& r : recents) list.push_back({{"path", r.path}, {"name", r.name}, {"opened", r.opened}});
  std::ofstream(recentFile()) << list.dump(2);
}

}  // namespace

std::vector<RecentProject> recentProjects() {
  std::vector<RecentProject> out;
  std::ifstream in(recentFile());
  if (!in) return out;
  const Json list = Json::parse(in, nullptr, false);
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
