#include "Editor.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <thread>

#include <GLFW/glfw3.h>
#include <nfd.hpp>

#include "Entities.hpp"
#include "audio/AudioModule.hpp"
#include "audio/SoundBuffer.hpp"
#include "Icons.hpp"
#include "LogBook.hpp"
#include "References.hpp"
#include "Thumbnails.hpp"
#include "UiThumbnails.hpp"
#include "editors/AssetEditor.hpp"
#include "panels/Panels.hpp"
#include "stb_image.h"

namespace fs = std::filesystem;

namespace {

double now() { return ImGui::GetTime(); }

std::string stemOf(const std::string& path) {
  std::string name = fs::path(path).filename().string();
  for (const char* suffix : {".scene.json", ".prefab.json", ".atlas.json", ".tileset.json", ".ui.html"}) {
    if (name.ends_with(suffix)) return name.substr(0, name.size() - std::strlen(suffix));
  }
  return fs::path(name).stem().string();
}

// "Sprite", "Sprite 2", ... unused in the scene. A numbered name counts on
// from its stem ("Sprite 2" → "Sprite 3", not "Sprite 2 2").
std::string uniqueName(const SceneDocument& doc, std::string base) {
  if (const size_t space = base.find_last_of(' '); space != std::string::npos && space + 1 < base.size() &&
      base.find_first_not_of("0123456789", space + 1) == std::string::npos) {
    base.resize(space);
  }
  auto taken = [&](const std::string& name) {
    for (size_t i = 0; i < doc.size(); ++i) {
      if (doc.entity(i).value("name", std::string()) == name) return true;
    }
    return false;
  };
  if (!taken(base)) return base;
  for (int n = 2;; ++n) {
    const std::string candidate = base + " " + std::to_string(n);
    if (!taken(candidate)) return candidate;
  }
}

std::vector<std::string> splitRows(const std::string& text) {
  std::vector<std::string> rows;
  std::string row;
  for (char c : text) {
    if (c == '\r') continue;
    if (c == '\n') {
      rows.push_back(std::move(row));
      row.clear();
    } else {
      row += c;
    }
  }
  if (!row.empty()) rows.push_back(std::move(row));
  while (!rows.empty() && rows.back().empty()) rows.pop_back();
  return rows;
}

void runDetached(std::string command) {
  std::thread([command = std::move(command)]() { std::system(command.c_str()); }).detach();
}

// A path as one shell argument.
std::string quoted(const std::string& s) {
#ifdef _WIN32
  return "\"" + s + "\"";
#else
  std::string out = "'";
  for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return out + "'";
#endif
}

bool hasProgram(const char* name) {
#ifdef _WIN32
  return std::system((std::string("where ") + name + " >nul 2>nul").c_str()) == 0;
#else
  return std::system((std::string("command -v ") + name + " >/dev/null 2>&1").c_str()) == 0;
#endif
}

}  // namespace

bool isTileTool(Tool tool) {
  return tool == Tool::TileBrush || tool == Tool::TileRect || tool == Tool::TileFill || tool == Tool::TileErase ||
         tool == Tool::TilePick;
}

Editor::Editor()
    : _welcome(std::make_unique<WelcomeScreen>()),
      _hierarchy(std::make_unique<HierarchyPanel>()),
      _inspector(std::make_unique<InspectorPanel>()),
      _scenePanel(std::make_unique<ScenePanel>()),
      _gamePanel(std::make_unique<GamePanel>()),
      _assets(std::make_unique<AssetsPanel>()),
      _console(std::make_unique<ConsolePanel>()),
      _palette(std::make_unique<CommandPalette>()),
      _export(std::make_unique<ExportDialog>()),
      _settings(std::make_unique<SettingsDialog>()) {
  registerCommands();
  loadSchemas();
}

Editor::~Editor() {
  UiThumbnails::instance().clear();  // while there's still a GL context
  _game.reset();
  _preview.stop();
}

void Editor::frame(float dt) {
  if (_project) watchFiles();
  autosave();
  saveAssets(false);
  if (auto done = _cli.takeFinished()) onBuildFinished(*done);
  const bool assetCapturing = std::any_of(_assetTabs.begin(), _assetTabs.end(), [&](const AssetTab& t) {
    return t.doc->path() == _activeAsset && t.view->capturesKeyboard();
  });
  _commands.handleShortcuts(gameHasKeyboard() || assetCapturing);

  if (_project) {
    drawWorkspace(dt);
  } else {
    _welcome->draw(*this);
  }
  _palette->draw(*this);
  _export->draw(*this);
  _settings->draw(*this);
  drawSavePrompt();
  drawPrompt();
  _toasts.draw();
  if (!_focusRequest.empty()) {
    ImGui::SetWindowFocus(_focusRequest.c_str());
    _focusRequest.clear();
  }
}

std::string Editor::windowTitle() const {
  if (!_project) return "Journeyman";
  std::string title = _project->name();
  if (_scene) title += " - " + _scene->title() + (_scene->dirty() ? " \xE2\x80\xA2" : "");
  return title + " - Journeyman";
}

void Editor::onKey(int key, int scancode, int action) {
  if (gameHasKeyboard()) {
    _game->key(key, scancode, action);
    return;
  }
  for (AssetTab& tab : _assetTabs) {
    if (tab.doc->path() == _activeAsset && tab.view->onKey(key, scancode, action)) return;
  }
}

bool Editor::busy() const { return playing() || _cli.busy() || _scenePanel->animating(); }

bool Editor::requestQuit() {
  if (_quitConfirmed) return true;
  saveAssets(true);
  whenSaved([this]() { _quitConfirmed = true; });
  return _quitConfirmed;
}

// ---- Project ---------------------------------------------------------------

bool Editor::openProject(const fs::path& folder) {
  std::string error;
  auto project = Project::open(folder, error);
  if (!project) {
    _toasts.show(Toasts::Kind::Error, "Couldn't open project", error);
    return false;
  }
  closeProject();
  _project = std::move(project);
  rememberProject(*_project);
  LogBook::instance().add(LogBook::Level::Info, LogBook::Source::Editor, "Opened " + _project->root().string());

  // Build first when sources are newer than the last build.
  std::error_code ec;
  const auto built = fs::last_write_time(_project->buildDir() / ".jm.json", ec);
  if (ec || built < _project->newestSource()) {
    build();
  } else {
    snapshotBuildInputs();
    restartPreview();
  }

  // Reopen the scene last edited here, else the game's first scene.
  std::string scene = lastScene(*_project);
  if (scene.empty() || !_project->file(scene)) scene = _project->manifest().value("entryScene", std::string());
  const auto scenes = _project->scenes();
  if (scene.empty() || !_project->file(scene)) scene = scenes.empty() ? std::string() : scenes.front();
  if (!scene.empty()) {
    std::string loadError;
    if (auto doc = SceneDocument::load(*_project, scene, loadError)) {
      _scene = std::move(doc);
      offerRecovery();
    }
  }
  return true;
}

void Editor::closeProject() {
  saveAssets(true);
  UiThumbnails::instance().clear();
  _assetTabs.clear();
  _activeAsset.clear();
  stopPlay();
  _preview.stop();
  _scene.reset();
  _selection.clear();
  _project.reset();
  _builtFiles.clear();
  _buildStale = false;
  Thumbnails::instance().clear();
}

void Editor::restartPreview() {
  if (!_project) return;
  if (!_preview.start(*_project)) {
    _toasts.show(Toasts::Kind::Error, "Scene preview unavailable", _preview.error(), "Show Console",
                 [this]() { focusPanel("Console"); });
    return;
  }
}

void Editor::loadSchemas() {
  // Component schemas come from the engine's registry, which only exists once
  // an engine starts; a tiny probe project gives one without waiting for a build.
  const fs::path probe = settingsDir() / "schema-probe";
  std::error_code ec;
  fs::create_directories(probe, ec);
  std::ofstream(probe / ".jm.json") << R"({"name": "probe", "scenes": [], "assets": []})";
  std::string error;
  MuteEngineLog mute;
  auto engine = HostedEngine::create(probe, {false, "", probe}, error);
  if (!engine) {
    LogBook::instance().add(LogBook::Level::Error, LogBook::Source::Editor, "Component schemas unavailable: " + error);
    return;
  }
  std::map<std::string, ComponentSchema> all;
  const ComponentRegistry& registry = engine->engine().getWorld().getComponentRegistry();
  registry.forEachRegisteredComponent([&](ComponentId id) {
    if (const ComponentInfo* info = registry.getInfo(id)) all[info->name] = info->schema;
  });
  setComponentSchemas(std::move(all));
}

// ---- Builds and file watching ------------------------------------------------

void Editor::build() {
  if (!_project || _cli.busy()) return;
  snapshotBuildInputs();
  _cli.start(_project->root(), {"build"}, "Build");
}

void Editor::snapshotBuildInputs() {
  _builtFiles.clear();
  for (const AssetFile& f : _project->files()) {
    if (f.kind != AssetKind::Folder) _builtFiles[f.path] = f.modified;
  }
  _buildStale = false;
  _changeSeen = 0;
}

void Editor::watchFiles() {
  if (now() - _lastScan < 0.75) return;
  _lastScan = now();
  refreshBuildState();
  // Rebuild shortly after changes settle (an editor saving several files at once).
  if (_buildStale && !_cli.busy() && now() - _changeSeen > 0.6) build();
}

void Editor::refreshBuildState() {
  if (!_project) return;
  if (!_project->rescan() && !_buildStale) return;

  size_t sources = 0;
  bool stale = false;
  for (const AssetFile& f : _project->files()) {
    if (f.kind == AssetKind::Folder || f.path.ends_with(".saving")) continue;
    ++sources;
    auto it = _builtFiles.find(f.path);
    stale |= it == _builtFiles.end() || it->second != f.modified;
  }
  stale |= sources != _builtFiles.size();
  if (stale && !_buildStale) _changeSeen = now();
  _buildStale = stale;
}

void Editor::onBuildFinished(const CliRunner::Finished& done) {
  if (done.label == "New Project") {
    if (!done.ok) _toasts.show(Toasts::Kind::Error, "Couldn't create the project", done.lastLine, "Show Console", [this]() { focusPanel("Console"); });
    return;
  }
  if (done.label == "Export") {
    if (done.ok) {
      const fs::path out = _project ? _project->root() / _exportOut : fs::path(_exportOut);
      _toasts.show(Toasts::Kind::Success, "Export complete", done.lastLine, "Reveal", [this, out]() { revealInFileManager(out); });
    } else {
      _toasts.show(Toasts::Kind::Error, "Export failed", done.lastLine, "Show Console", [this]() { focusPanel("Console"); });
    }
    return;
  }
  _lastBuildFailed = !done.ok;
  if (auto queued = std::exchange(_exportAfterBuild, std::nullopt); queued && done.ok) exportGame(*queued, _exportOut);
  if (!done.ok) {
    _playAfterBuild = false;
    _toasts.show(Toasts::Kind::Error, "Build failed", done.lastLine, "Show Console", [this]() { focusPanel("Console"); });
    return;
  }
  ++_buildGeneration;
  restartPreview();
  Thumbnails::instance().clear();
  if (_playAfterBuild) {
    _playAfterBuild = false;
    startPlay(_playFrom);
  }
}

void Editor::writeThrough(const std::string& path) {
  if (!_project) return;
  // Only files the build copies as they are; scripts compile and atlases pack,
  // so those wait for the rebuild the change triggers.
  if (path.ends_with(".ts") || path.ends_with(".atlas.json")) return;
  std::error_code ec;
  const fs::path to = _project->buildDir() / path;
  if (!fs::exists(_project->buildDir() / ".jm.json", ec)) return;
  fs::create_directories(to.parent_path(), ec);
  fs::copy_file(_project->abs(path), to, fs::copy_options::overwrite_existing, ec);
  if (!ec) _builtFiles[path] = fs::last_write_time(_project->abs(path), ec);
}

void Editor::exportGame(std::vector<std::string> args, std::string outDir) {
  if (!_project) return;
  _exportOut = outDir;
  if (_cli.busy()) {  // runs once the build finishes
    _exportAfterBuild = args;
    _toasts.show(Toasts::Kind::Info, "Export queued", "It starts when the current build finishes.");
    return;
  }
  // A current build needs no rebuilding (nor Node) to export.
  refreshBuildState();
  if (!_buildStale && !_lastBuildFailed && fs::exists(_project->buildDir() / ".jm.json")) args.insert(args.begin(), "--skip-build");
  args.insert(args.begin(), "export");
  args.push_back("--out");
  args.push_back(outDir);
  _cli.start(_project->root(), args, "Export");
  focusPanel("Console");
}

// ---- Scenes --------------------------------------------------------------------

void Editor::whenCurrentSaved(std::function<void()> then) {
  if (_scene && _scene->dirty()) {
    _afterSave = std::move(then);
    _askSave = true;
    return;
  }
  then();
}

void Editor::whenSaved(std::function<void()> then) {
  // Both the open prefab and the scene waiting behind it.
  whenCurrentSaved([this, then = std::move(then)]() {
    if (!_prefabReturn) return then();
    leavePrefab();
    whenCurrentSaved(then);
  });
}

void Editor::runAfterSave() {
  auto then = std::move(_afterSave);
  _afterSave = nullptr;
  if (then) then();
}

void Editor::openScene(const std::string& path) {
  if (!_project) return;
  if (_scene && _scene->path() == path) return;
  if (_prefabReturn && _prefabReturn->scene->path() == path) return returnFromPrefab();
  // Another prefab from prefab mode keeps the scene behind; anything else leaves it.
  auto guard = _prefabReturn && path.ends_with(".prefab.json") ? &Editor::whenCurrentSaved : &Editor::whenSaved;
  (this->*guard)([this, path]() {
    std::string error;
    auto doc = SceneDocument::load(*_project, path, error);
    if (!doc) {
      _toasts.show(Toasts::Kind::Error, "Couldn't open scene", error);
      return;
    }
    if (playing()) stopPlay();
    _scene = std::move(doc);
    rememberScene(*_project, path);
    offerRecovery();
    _selection.clear();
    focusPanel("Scene");
  });
}

void Editor::newScene() {
  if (!_project) return;
  whenSaved([this]() {
    std::string path;
    for (int n = 1;; ++n) {
      path = "scenes/" + std::string(n == 1 ? "untitled" : "untitled_" + std::to_string(n)) + ".scene.json";
      if (!_project->file(path)) break;
    }
    _scene = SceneDocument::create(path);
    _selection.clear();
    focusPanel("Scene");
  });
}

fs::path Editor::recoveryFile() const {
  std::string key = _project->root().string() + "/" + _scene->path();
  for (char& c : key) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.') c = '_';
  }
  return settingsDir() / "recovery" / (key + ".json");
}

void Editor::autosave() {
  if (!_project || !_scene || !_scene->dirty() || ImGui::GetTime() - _lastAutosave < 20.0) return;
  _lastAutosave = ImGui::GetTime();
  std::error_code ec;
  fs::create_directories(recoveryFile().parent_path(), ec);
  std::ofstream(recoveryFile(), std::ios::binary) << _scene->serialized();
}

void Editor::offerRecovery() {
  // A recovery file newer than the scene means the editor stopped with unsaved changes.
  std::error_code ec;
  const fs::path recovery = recoveryFile();
  if (!fs::exists(recovery, ec)) return;
  const auto saved = fs::last_write_time(_project->abs(_scene->path()), ec);
  if (ec || fs::last_write_time(recovery, ec) <= saved) {
    fs::remove(recovery, ec);
    return;
  }
  _toasts.show(Toasts::Kind::Warning, "Unsaved changes were recovered", _scene->title() + " has edits from a session that didn't save.",
               "Restore", [this, recovery]() {
                 std::ifstream in(recovery, std::ios::binary);
                 Json restored = Json::parse(in, nullptr, false);
                 if (restored.is_discarded() || !_scene) return;
                 _scene->edit("Restore unsaved changes", [&](Json& doc) {
                   for (auto it = restored.begin(); it != restored.end(); ++it) doc[it.key()] = it.value();
                 });
               });
}

void Editor::prompt(std::string title, std::string label, std::string initial, std::function<void(const std::string&)> done,
                    std::function<std::string(const std::string&)> where) {
  _prompt = Prompt{std::move(title), std::move(label), std::move(initial), std::move(done), std::move(where)};
}

void Editor::saveSceneAs() {
  if (!_project || !_scene) return;
  const bool prefab = _scene->isPrefab();
  prompt(prefab ? "Save Prefab As" : "Save Scene As", "Name", _scene->title(), [this, prefab](const std::string& name) {
    const std::string folder = std::filesystem::path(_scene->path()).parent_path().generic_string();
    const std::string path = (folder.empty() ? std::string(prefab ? "assets/prefabs" : "scenes") : folder) + "/" + name +
                             (prefab ? ".prefab.json" : ".scene.json");
    if (path != _scene->path() && _project->file(path)) {
      _toasts.show(Toasts::Kind::Warning, "That name is taken", path + " already exists.");
      return;
    }
    std::string error;
    if (!_scene->saveAs(*_project, path, error)) {
      _toasts.show(Toasts::Kind::Error, "Couldn't save", error);
      return;
    }
    saveScene();
    rememberScene(*_project, path);
  });
}

bool Editor::saveScene() {
  if (!_project || !_scene) return false;
  if (_scene->unsaved()) {  // a new scene gets its name first
    saveSceneAs();
    return false;
  }
  std::string error;
  if (!_scene->save(*_project, error)) {
    _toasts.show(Toasts::Kind::Error, "Couldn't save", error);
    return false;
  }
  std::error_code ec;
  fs::remove(recoveryFile(), ec);
  writeThrough(_scene->path());
  for (const std::string& map : _scene->mapFiles()) writeThrough(map);
  // A new scene joins the manifest so the game can load it.
  Json& scenes = _project->manifest()["scenes"];
  if (!scenes.is_array()) scenes = Json::array();
  if (std::find(scenes.begin(), scenes.end(), Json(_scene->path())) == scenes.end()) {
    scenes.push_back(_scene->path());
    std::string manifestError;
    if (!_project->saveManifest(manifestError)) _toasts.show(Toasts::Kind::Error, "Couldn't update .jm.json", manifestError);
  }
  _project->rescan();
  return true;
}

// ---- Selection ------------------------------------------------------------------

bool Editor::isSelected(EntityUid uid) const {
  return std::find(_selection.begin(), _selection.end(), uid) != _selection.end();
}

void Editor::inspectAsset(const std::string& path) {
  _inspectedAsset = path;
  _selection.clear();
  _activeAsset.clear();  // the Inspector shows the file, not an asset tab's selection
}

bool Editor::moveAsset(const std::string& from, const std::string& to) {
  if (!_project || from == to) return false;
  std::error_code ec;
  if (_project->file(to) || fs::exists(_project->abs(to), ec)) {
    _toasts.show(Toasts::Kind::Warning, "Can't move " + fs::path(from).filename().string(), to + " already exists.");
    return false;
  }
  fs::create_directories(_project->abs(to).parent_path(), ec);
  fs::rename(_project->abs(from), _project->abs(to), ec);
  if (ec) {
    _toasts.show(Toasts::Kind::Error, "Couldn't move " + fs::path(from).filename().string(), ec.message());
    return false;
  }
  // The build mirrors source paths: move what it made too, so nothing goes missing before the rebuild.
  if (fs::exists(_project->buildDir() / from, ec)) {
    fs::create_directories((_project->buildDir() / to).parent_path(), ec);
    fs::rename(_project->buildDir() / from, _project->buildDir() / to, ec);
  }
  // References are quoted paths: "from", "from#region", or "from/..." for folders.
  auto rewrite = [&](std::string text, int& count) {
    for (const std::string& tail : {std::string("\""), std::string("#"), std::string("/")}) {
      const std::string needle = "\"" + from + tail, replacement = "\"" + to + tail;
      for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + replacement.size())) {
        text.replace(at, needle.size(), replacement);
        ++count;
      }
    }
    return text;
  };
  int references = 0, files = 0;
  _project->rescan();
  for (const AssetFile& f : _project->files()) {
    const std::string ext = fs::path(f.path).extension().string();
    if (f.kind == AssetKind::Folder || (ext != ".json" && ext != ".ts" && ext != ".html" && ext != ".css")) continue;
    int count = 0;
    const std::string before = _project->readText(f.path);
    const std::string after = rewrite(before, count);
    if (count == 0) continue;
    std::string error;
    if (_project->writeText(f.path, after, error)) {
      references += count;
      ++files;
      writeThrough(f.path);
    }
  }
  // The manifest (not under assets/) and the open scene.
  {
    int count = 0;
    const std::string manifest = rewrite(_project->manifest().dump(2), count);
    if (count > 0) {
      _project->manifest() = Json::parse(manifest);
      std::string error;
      _project->saveManifest(error);
      references += count;
    }
  }
  if (_scene) {
    if (_scene->path() == from || _scene->path().starts_with(from + "/")) {
      std::string error;
      _scene->saveAs(*_project, to + _scene->path().substr(from.size()), error);
    } else if (_scene->dirty()) {
      int count = 0;
      const Json patched = Json::parse(rewrite(_scene->serialized(), count));
      if (count > 0) _scene->edit("Update references", [&](Json& doc) {
        for (auto it = patched.begin(); it != patched.end(); ++it) doc[it.key()] = it.value();
      });
    } else {
      // Clean: the rewritten file is the truth.
      std::string error;
      if (auto doc = SceneDocument::load(*_project, _scene->path(), error)) {
        _scene = std::move(doc);
        _selection.clear();
      }
    }
  }
  _project->rescan();
  _preview.invalidate();
  if (references > 0) {
    _toasts.show(Toasts::Kind::Success, "Moved " + fs::path(from).filename().string(),
                 "Updated " + std::to_string(references) + (references == 1 ? " reference" : " references") + " in " +
                     std::to_string(files + 1) + (files == 0 ? " file" : " files") + ".");
  }
  return true;
}

void Editor::deleteAsset(const std::string& path) {
  if (!_project) return;
  const auto users = referencesTo(*_project, path);  // before it's gone: what still names it
  std::error_code ec;
  const fs::path trash = settingsDir() / "trash" / _project->root().filename() / path;
  fs::create_directories(trash.parent_path(), ec);
  fs::remove_all(trash, ec);
  fs::rename(_project->abs(path), trash, ec);
  if (ec) {
    _toasts.show(Toasts::Kind::Error, "Couldn't delete " + fs::path(path).filename().string(), ec.message());
    return;
  }
  // A deleted scene leaves the manifest, so the build keeps working.
  Json& scenes = _project->manifest()["scenes"];
  const bool listed = scenes.is_array() && std::find(scenes.begin(), scenes.end(), Json(path)) != scenes.end();
  if (listed) {
    scenes.erase(std::remove(scenes.begin(), scenes.end(), Json(path)), scenes.end());
    std::string error;
    _project->saveManifest(error);
  }
  if (_scene && _scene->path() == path) {
    _scene.reset();
    _selection.clear();
  }
  if (_inspectedAsset.starts_with(path)) _inspectedAsset.clear();
  _project->rescan();
  // Recoverable until the same path is deleted again.
  std::string usedIn;
  for (const std::string& u : users) {
    if (u != ".jm.json") usedIn += (usedIn.empty() ? "" : ", ") + fs::path(u).filename().string();
  }
  _toasts.show(usedIn.empty() ? Toasts::Kind::Info : Toasts::Kind::Warning, "Deleted " + fs::path(path).filename().string(),
               usedIn.empty() ? "" : "Still named in " + usedIn, "Undo", [this, path, trash, listed]() {
    std::error_code undoError;
    fs::rename(trash, _project->abs(path), undoError);
    if (listed) {
      _project->manifest()["scenes"].push_back(path);
      std::string error;
      _project->saveManifest(error);
    }
    _project->rescan();
  });
}

namespace {

// "player" → "player", "player_2", ... free in `folder`.
std::string uniquePrefabPath(const Project& project, const std::string& folder, const std::string& name) {
  for (int n = 1;; ++n) {
    const std::string path = folder + "/" + name + (n == 1 ? "" : "_" + std::to_string(n)) + ".prefab.json";
    if (!project.file(path)) return path;
  }
}

// File-name-safe: letters, digits, _ and -; spaces become _.
std::string safeName(const std::string& name) {
  std::string out;
  for (char c : name) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    else if (c == ' ' && !out.empty() && out.back() != '_') out += '_';
  }
  return out.empty() ? std::string("prefab") : out;
}

}  // namespace

std::string Editor::createPrefab(EntityUid uid, const std::string& folder, std::string name) {
  if (!_project || !_scene || _scene->isPrefab()) return {};
  const int index = _scene->indexOf(uid);
  if (index < 0) return {};
  const Json entity = _scene->entity(static_cast<size_t>(index));
  if (name.empty()) name = _scene->displayName(static_cast<size_t>(index));
  const std::string path = uniquePrefabPath(*_project, folder.empty() ? "assets/prefabs" : folder, safeName(name));

  // The prefab keeps everything but where this one stands; the instance keeps that.
  Json components = effectiveComponents(*_project, entity);
  Json position;
  if (components.contains("TransformComponent") && components["TransformComponent"].contains("position")) {
    position = components["TransformComponent"]["position"];
    components["TransformComponent"]["position"] = Json::array({0, 0, position.size() > 2 ? position[2] : Json(0)});
  }
  Json prefab = {{"components", components}, {"tags", Json::array()}};
  wholeNumbersAsIntegers(prefab);
  std::string error;
  if (!_project->writeText(path, prefab.dump(2) + "\n", error)) {
    _toasts.show(Toasts::Kind::Error, "Couldn't save the prefab", error);
    return {};
  }
  addedFile(path);
  _scene->editEntity(uid, "Make prefab " + fs::path(path).stem().stem().string(), [&](Json& e) {
    e.erase("components");
    e.erase("overrides");
    e["prefab"] = path;
    if (!position.is_null()) e["overrides"] = {{"TransformComponent", {{"position", position}}}};
  });
  _toasts.show(Toasts::Kind::Success, "Made a prefab", path + "\nDrag it into scenes to place more.", "Show",
               [this, path]() { revealAsset(path); });
  return path;
}

void Editor::newPrefab(const std::string& folder) {
  if (!_project) return;
  const std::string path = uniquePrefabPath(*_project, folder.empty() ? "assets/prefabs" : folder, "new_prefab");
  Json prefab = {{"components", {{"TransformComponent", {{"position", {0, 0, 0}}, {"scale", {16, 16}}}},
                                 {"SpriteComponent", {{"texture", ""}, {"color", {1, 1, 1, 1}}}}}},
                 {"tags", Json::array()}};
  std::string error;
  if (!_project->writeText(path, prefab.dump(2) + "\n", error)) {
    _toasts.show(Toasts::Kind::Error, "Couldn't create the prefab", error);
    return;
  }
  addedFile(path);
  editPrefab(path);
}

void Editor::applyOverrides(EntityUid uid, const std::string& component) {
  if (!_project || !_scene) return;
  const Json* found = _scene->find(uid);
  if (!found || !found->contains("prefab")) return;
  const Json entity = *found;
  const std::string path = entity["prefab"].get<std::string>();
  const std::string before = _project->readText(path);
  Json prefab = Json::parse(before, nullptr, false);
  if (prefab.is_discarded()) {
    _toasts.show(Toasts::Kind::Error, "Couldn't read the prefab", path);
    return;
  }
  // What moves into the prefab: the chosen component's overrides, or all but where this one stands.
  Json applied = Json::object();
  for (const auto& [name, fields] : entity.value("overrides", Json::object()).items()) {
    if (name == "tags" || (!component.empty() && name != component)) continue;
    Json moving = fields;
    if (component.empty() && name == "TransformComponent" && moving.is_object()) moving.erase("position");
    if (moving.is_object() && moving.empty()) continue;
    applied[name] = moving;
  }
  if (applied.empty()) return;
  Json& components = prefab["components"];
  for (const auto& [name, fields] : applied.items()) {
    components[name] = components.contains(name) ? mergeDeep(components[name], fields) : fields;
  }
  wholeNumbersAsIntegers(prefab);
  std::string error;
  if (!_project->writeText(path, prefab.dump(2) + "\n", error)) {
    _toasts.show(Toasts::Kind::Error, "Couldn't save the prefab", error);
    return;
  }
  writeThrough(path);
  const std::string label = "Apply to " + fs::path(path).filename().string();
  _scene->editEntity(uid, label, [&](Json& e) {
    for (const auto& [name, fields] : applied.items()) {
      if (!e.contains("overrides") || !e["overrides"].contains(name)) continue;
      Json& left = e["overrides"][name];
      for (const auto& [key, _] : fields.items()) left.erase(key);
      if (left.empty()) e["overrides"].erase(name);
    }
    if (e.contains("overrides") && e["overrides"].empty()) e.erase("overrides");
  });
  _preview.invalidate();  // every instance changes
  _toasts.show(Toasts::Kind::Success, "Applied to " + fs::path(path).filename().string(), "Every instance has the change now.", "Undo",
               [this, path, before, label]() {
                 std::string undoError;
                 _project->writeText(path, before, undoError);
                 writeThrough(path);
                 if (_scene && _scene->canUndo() && _scene->undoLabel() == label) _scene->undo();
                 _preview.invalidate();
               });
}

void Editor::revertOverrides(EntityUid uid, const std::string& component) {
  if (!_scene) return;
  _scene->editEntity(uid, component.empty() ? "Revert to prefab" : "Revert " + componentLabel(component), [&](Json& e) {
    if (!e.contains("overrides")) return;
    if (component.empty()) {
      // Keep where it stands: that's the instance's, not an edit.
      const Json position = e["overrides"].value("TransformComponent", Json::object()).value("position", Json());
      e.erase("overrides");
      if (!position.is_null()) e["overrides"] = {{"TransformComponent", {{"position", position}}}};
    } else {
      e["overrides"].erase(component);
      if (e["overrides"].empty()) e.erase("overrides");
    }
  });
}

void Editor::editPrefab(const std::string& path) {
  if (!_project) return;
  if (!_scene || _scene->isPrefab()) return openScene(path);
  // Step into the prefab without closing the scene: no save prompt, nothing lost.
  std::string error;
  auto doc = SceneDocument::load(*_project, path, error);
  if (!doc) {
    _toasts.show(Toasts::Kind::Error, "Couldn't open prefab", error);
    return;
  }
  if (playing()) stopPlay();
  _prefabReturn = PrefabReturn{std::make_unique<SceneDocument>(std::move(*_scene)), _selection};
  _scene = std::move(doc);
  _selection.clear();
  if (_scene->size() > 0) _selection.push_back(_scene->uid(0));  // the prefab's root, ready to edit
  focusPanel("Scene");
}

void Editor::leavePrefab() {
  if (!_prefabReturn) return;
  _scene = std::move(*_prefabReturn->scene);
  _selection = std::move(_prefabReturn->selection);
  _prefabReturn.reset();
  _preview.invalidate();  // the prefab may have changed under its instances
  focusPanel("Scene");
}

void Editor::returnFromPrefab() {
  if (_prefabReturn) whenCurrentSaved([this]() { leavePrefab(); });
}

std::vector<EntityUid> Editor::instancesOf(const std::string& prefab) const {
  std::vector<EntityUid> out;
  if (!_scene) return out;
  for (size_t i = 0; i < _scene->size(); ++i) {
    if (_scene->entity(i).value("prefab", std::string()) == prefab) out.push_back(_scene->uid(i));
  }
  return out;
}

void Editor::importFiles(const std::vector<fs::path>& files, const std::string& folder) {
  if (!_project) return;
  const std::string into = folder.empty() ? std::string("assets") : folder;
  int copied = 0;
  std::string last;
  for (const fs::path& file : files) {
    std::error_code ec;
    const fs::path target = _project->abs(into) / file.filename();
    if (fs::is_directory(file, ec)) {
      fs::copy(file, target, fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
    } else {
      fs::create_directories(target.parent_path(), ec);
      fs::copy_file(file, target, fs::copy_options::overwrite_existing, ec);
    }
    if (ec) {
      _toasts.show(Toasts::Kind::Error, "Couldn't import " + file.filename().string(), ec.message());
      continue;
    }
    ++copied;
    last = into + "/" + file.filename().string();
    // In the build, and usable in the preview right away.
    if (fs::is_regular_file(target, ec)) {
      addedFile(last);
    } else {
      for (const auto& entry : fs::recursive_directory_iterator(target, ec)) {
        if (entry.is_regular_file()) addedFile(fs::relative(entry.path(), _project->root()).generic_string());
      }
    }
  }
  if (copied == 0) return;
  refreshBuildState();
  _toasts.show(Toasts::Kind::Success, copied == 1 ? "Imported " + fs::path(last).filename().string() : "Imported " + std::to_string(copied) + " files",
               "Into " + into + "/", "Show", [this, last]() { revealAsset(last); });
}

void Editor::select(EntityUid uid, SelectMode mode) {
  _inspectedAsset.clear();
  if (mode == SelectMode::Replace) {
    _selection = {uid};
    return;
  }
  auto it = std::find(_selection.begin(), _selection.end(), uid);
  if (it != _selection.end()) {
    if (mode == SelectMode::Toggle) _selection.erase(it);
    return;
  }
  _selection.insert(_selection.begin(), uid);
}

void Editor::selectAll(std::vector<EntityUid> uids) {
  _inspectedAsset.clear();
  _selection = std::move(uids);
}

void Editor::duplicateSelection() {
  if (!_scene || _scene->isPrefab() || _selection.empty()) return;
  std::vector<EntityUid> copies;
  _scene->edit(_selection.size() == 1 ? "Duplicate " + _scene->displayName(_scene->indexOf(primary())) : "Duplicate",
               [&](Json& doc) {
                 Json& list = doc["entities"];
                 for (EntityUid uid : _selection) {
                   for (size_t i = 0; i < list.size(); ++i) {
                     if (list[i].value(kUidKey, EntityUid{0}) != uid) continue;
                     Json copy = list[i];
                     copy.erase(kUidKey);
                     if (copy.contains("name")) copy["name"] = uniqueName(*_scene, copy["name"].get<std::string>());
                     list.insert(list.begin() + static_cast<std::ptrdiff_t>(i) + 1, copy);
                     break;
                   }
                 }
               });
  // The copies are the entries right after each original.
  for (EntityUid uid : _selection) {
    const int i = _scene->indexOf(uid);
    if (i >= 0 && i + 1 < static_cast<int>(_scene->size())) copies.push_back(_scene->uid(i + 1));
  }
  _selection = copies;
}

namespace {
constexpr const char* kClipboardKey = "journeymanEntities";
}  // namespace

void Editor::copySelection() {
  if (!_scene || _scene->isPrefab() || _selection.empty()) return;
  Json list = Json::array();
  for (size_t i = 0; i < _scene->size(); ++i) {
    if (!isSelected(_scene->uid(i))) continue;
    Json e = _scene->entity(i);
    e.erase(kUidKey);
    list.push_back(std::move(e));
  }
  ImGui::SetClipboardText(Json{{kClipboardKey, list}}.dump(2).c_str());
}

void Editor::paste() {
  if (!_scene || _scene->isPrefab()) return;
  const char* text = ImGui::GetClipboardText();
  const Json clip = Json::parse(text ? text : "", nullptr, false);
  if (!clip.is_object() || !clip.contains(kClipboardKey)) {
    _toasts.show(Toasts::Kind::Info, "Nothing to paste", "Copy entities first.");
    return;
  }
  const size_t before = _scene->size();
  _scene->edit("Paste", [&](Json& doc) {
    for (Json e : clip[kClipboardKey]) {
      if (e.contains("name")) e["name"] = uniqueName(*_scene, e["name"].get<std::string>());
      doc["entities"].push_back(std::move(e));
    }
  });
  _selection.clear();
  for (size_t i = before; i < _scene->size(); ++i) _selection.push_back(_scene->uid(i));
}

void Editor::deleteSelection() {
  if (!_scene || _scene->isPrefab() || _selection.empty()) return;
  const std::string label =
      _selection.size() == 1 ? "Delete " + _scene->displayName(_scene->indexOf(primary())) : "Delete Entities";
  _scene->removeEntities(_selection, label);
  _selection.clear();
}

EntityUid Editor::createEntity(const std::string& kind, glm::vec2 at) {
  if (!_scene || _scene->isPrefab()) return 0;
  const Json position = Json::array({std::round(at.x), std::round(at.y), 0});
  Json components = Json::object();
  std::string name = kind;
  if (kind == "Empty") {
    name = "Entity";
    components["TransformComponent"] = {{"position", position}};
  } else if (kind == "Sprite") {
    components["TransformComponent"] = {{"position", position}, {"scale", {16, 16}}};
    components["SpriteComponent"] = {{"texture", ""}, {"color", {1, 1, 1, 1}}};
  } else if (kind == "Text") {
    components["TransformComponent"] = {{"position", position}};
    components["TextComponent"] = {{"text", "Text"}, {"size", 16}, {"color", {1, 1, 1, 1}}};
  } else if (kind == "Tile Map") {
    std::string tileset;
    for (const AssetFile& f : _project->files()) {
      if (f.kind == AssetKind::Tileset) {
        tileset = f.path;
        break;
      }
    }
    components["TransformComponent"] = {{"position", position}};
    components["TileMapComponent"] = {{"tileset", tileset}, {"tileSize", 16},
                                      {"rows", Json::array({std::string(20, ' '), std::string(20, ' '), std::string(20, ' '),
                                                            std::string(20, ' '), std::string(20, ' '), std::string(20, ' ')})}};
  } else if (kind == "UI Screen") {
    components["UIDocumentComponent"] = {{"src", ""}, {"order", 0}};
  } else if (kind == "Sound") {
    components["AudioEmitterComponent"] = {{"sound", ""}, {"gain", 1}, {"looping", false}, {"bus", "sfx"}};
  } else if (kind == "Script") {
    components["ScriptComponent"] = {{"script", ""}};
  }
  const EntityUid uid = _scene->addEntity({{"name", uniqueName(*_scene, name)}, {"components", components}},
                                          "Create " + name);
  _selection = {uid};
  return uid;
}

EntityUid Editor::instantiateAsset(const std::string& path, glm::vec2 at) {
  if (!_scene || !_project) return 0;
  const AssetKind kind = assetKindOf(path.substr(0, path.find('#')));
  if (kind == AssetKind::Scene) {
    openScene(path);
    return 0;
  }
  if (_scene->isPrefab()) return 0;
  const Json position = Json::array({std::round(at.x), std::round(at.y), 0});
  Json entry = {{"name", uniqueName(*_scene, stemOf(path.substr(path.find('#') == std::string::npos ? 0 : path.find('#') + 1)))}};
  Json components = Json::object();

  switch (kind) {
    case AssetKind::Prefab: {
      entry["name"] = uniqueName(*_scene, stemOf(path));
      entry["prefab"] = path;
      const Json* prefab = prefabJson(*_project, path);
      if (prefab && prefab->value("components", Json::object()).contains("TransformComponent")) {
        const Json base = (*prefab)["components"]["TransformComponent"].value("position", Json::array({0, 0, 0}));
        entry["overrides"] = {{"TransformComponent", {{"position", {position[0], position[1], base.size() > 2 ? base[2] : Json(0)}}}}};
      }
      _selection = {_scene->addEntity(entry, "Add " + stemOf(path))};
      return _selection.front();
    }
    case AssetKind::Image:
    case AssetKind::Atlas: {
      glm::vec2 half(16.0f);
      if (auto picture = Thumbnails::instance().get(*_project, path)) half = {picture->size.x * 0.5f, picture->size.y * 0.5f};
      components["TransformComponent"] = {{"position", position}, {"scale", {half.x, half.y}}};
      components["SpriteComponent"] = {{"texture", path}};
      break;
    }
    case AssetKind::Tileset:
    case AssetKind::Map: {
      std::string tileset = kind == AssetKind::Tileset ? path : std::string();
      if (tileset.empty()) {
        for (const AssetFile& f : _project->files()) {
          if (f.kind == AssetKind::Tileset && fs::path(f.path).parent_path() == fs::path(path).parent_path()) tileset = f.path;
        }
      }
      components["TransformComponent"] = {{"position", position}};
      components["TileMapComponent"] = {{"tileset", tileset}, {"tileSize", 16}};
      components["TileMapComponent"]["rows"] =
          kind == AssetKind::Map ? Json(path) : Json::array({std::string(20, ' '), std::string(20, ' '), std::string(20, ' ')});
      break;
    }
    case AssetKind::Ui:
      components["UIDocumentComponent"] = {{"src", path}, {"order", 0}};
      break;
    case AssetKind::Sound:
      components["AudioEmitterComponent"] = {{"sound", path}, {"gain", 1}, {"looping", false}, {"bus", "sfx"}};
      break;
    case AssetKind::Script:
      components["ScriptComponent"] = {{"script", path}};
      break;
    default:
      _toasts.show(Toasts::Kind::Info, "Can't place this file", path + " isn't something a scene can hold.");
      return 0;
  }
  entry["components"] = components;
  const EntityUid uid = _scene->addEntity(entry, "Add " + entry["name"].get<std::string>());
  _selection = {uid};
  return uid;
}

bool Editor::applyAssetToEntity(EntityUid uid, const std::string& path, bool dryRun) {
  if (!_scene || !_project || !_scene->find(uid)) return false;
  const AssetKind kind = assetKindOf(path.substr(0, path.find('#')));
  const char* component = nullptr;
  const char* key = nullptr;
  switch (kind) {
    case AssetKind::Script: component = "ScriptComponent", key = "script"; break;
    case AssetKind::Image:
    case AssetKind::Atlas: component = "SpriteComponent", key = "texture"; break;
    case AssetKind::Sound: component = "AudioEmitterComponent", key = "sound"; break;
    case AssetKind::Ui: component = "UIDocumentComponent", key = "src"; break;
    case AssetKind::Tileset: component = "TileMapComponent", key = "tileset"; break;
    default: return false;
  }
  if (kind == AssetKind::Atlas && path.find('#') == std::string::npos) return false;  // a whole atlas isn't a picture
  if (dryRun) return true;
  const std::string name = _scene->displayName(static_cast<size_t>(_scene->indexOf(uid)));
  _scene->editEntity(uid, "Set " + componentLabel(component) + " of " + name, [&](Json& e) {
    editableComponent(e, component)[key] = path;
    // A new sprite takes the picture's size.
    if (kind == AssetKind::Image || kind == AssetKind::Atlas) {
      const Json scale = fieldValue(*_project, e, "TransformComponent", "scale");
      const bool unsized = !scale.is_array() || (scale.size() >= 2 && scale[0] == 1 && scale[1] == 1);
      if (auto picture = Thumbnails::instance().get(*_project, path); picture && unsized) {
        editableComponent(e, "TransformComponent")["scale"] = {picture->size.x * 0.5f, picture->size.y * 0.5f};
      }
    }
  });
  _selection = {uid};
  return true;
}

Json Editor::resolveForEngine(const Json& entity) {
  Json out = {{"name", entity.value("name", std::string())}, {kUidKey, entity.value(kUidKey, EntityUid{0})}};
  Json components = _project ? effectiveComponents(*_project, entity) : entity.value("components", Json::object());
  if (_scene && components.contains("TileMapComponent")) {
    Json& map = components["TileMapComponent"];
    if (map.value("rows", Json()).is_string()) {
      if (const Json* rows = _scene->mapFile(map["rows"].get<std::string>())) map["rows"] = *rows;
    }
  }
  out["components"] = std::move(components);
  return out;
}

// ---- Tile maps --------------------------------------------------------------------

std::vector<std::string> Editor::mapRows(EntityUid uid) {
  if (!_scene || !_project) return {};
  const Json* entity = _scene->find(uid);
  if (!entity) return {};
  const Json map = effectiveComponents(*_project, *entity).value("TileMapComponent", Json::object());
  const Json rows = map.value("rows", Json());
  if (rows.is_array()) return rows.get<std::vector<std::string>>();
  if (!rows.is_string()) return {};
  if (const Json* painted = _scene->mapFile(rows.get<std::string>())) return painted->get<std::vector<std::string>>();
  return splitRows(_project->readText(rows.get<std::string>()));
}

void Editor::setMapRows(EntityUid uid, std::vector<std::string> rows, const std::string& label, const std::string& mergeKey) {
  if (!_scene || !_project) return;
  const Json* entity = _scene->find(uid);
  if (!entity) return;
  const Json map = effectiveComponents(*_project, *entity).value("TileMapComponent", Json::object());
  const Json source = map.value("rows", Json());
  if (source.is_string()) {
    const std::string file = source.get<std::string>();
    _scene->edit(label, [&](Json& doc) { doc[kMapsKey][file] = rows; }, mergeKey);
    return;
  }
  // Inline rows: the entry's own component, or an override on its prefab's.
  const bool prefabInstance = entity->contains("prefab");
  _scene->editEntity(uid, label, [&](Json& e) {
    if (prefabInstance) {
      e["overrides"]["TileMapComponent"]["rows"] = rows;
    } else {
      e["components"]["TileMapComponent"]["rows"] = rows;
    }
  }, mergeKey);
}

// ---- Play -------------------------------------------------------------------------

void Editor::playSceneFile() {
  // The game sees the scene as edited, saved or not: write it (and any painted
  // maps) into build/, where the running game reads files from.
  std::error_code ec;
  const fs::path target = _project->buildDir() / _scene->path();
  fs::create_directories(target.parent_path(), ec);
  std::ofstream(target, std::ios::binary) << _scene->serialized();
  for (const std::string& map : _scene->mapFiles()) {
    std::string text;
    for (const auto& row : *_scene->mapFile(map)) text += row.get<std::string>() + "\n";
    std::ofstream(_project->buildDir() / map, std::ios::binary) << text;
  }
}

void Editor::startPlay(PlayFrom from) {
  if (!_project || playing()) return;
  const std::string first = _project->manifest().value("entryScene", std::string());
  if (from == PlayFrom::Scene && !_scene) {
    _toasts.show(Toasts::Kind::Info, "Nothing to play", "Open a scene first.");
    return;
  }
  if (from == PlayFrom::Scene && _scene->isPrefab()) {
    _toasts.show(Toasts::Kind::Info, "Prefabs don't play on their own", "Open a scene that uses it, or play the game.");
    return;
  }
  if (from == PlayFrom::Game && first.empty()) {
    _toasts.show(Toasts::Kind::Info, "The project has no first scene", "Choose one in Project Settings.", "Settings",
                 [this]() { _settings->open(); });
    return;
  }
  refreshBuildState();
  if (_cli.busy() || _buildStale || !fs::exists(_project->buildDir() / ".jm.json")) {
    _playAfterBuild = true;
    _playFrom = from;
    if (!_cli.busy()) build();
    focusPanel("Game");
    return;
  }
  if (_scene && !_scene->isPrefab()) playSceneFile();
  const std::string entry = from == PlayFrom::Game ? first : _scene->path();
  HostedEngine::Options options;
  options.simulate = true;
  options.entryScene = entry;
  options.saveDir = settingsDir() / "play-saves" / _project->root().filename();
  std::string error;
  _game = HostedEngine::create(_project->buildDir(), options, error);
  if (!_game) {
    _toasts.show(Toasts::Kind::Error, "The game didn't start", error, "Show Console", [this]() { focusPanel("Console"); });
    return;
  }
  if (_clearConsoleOnPlay) LogBook::instance().clear();
  _showLive = true;
  _liveSelection.reset();
  _paused = false;
  _gameFocused = true;
  focusPanel("Game");
}

void Editor::stopPlay() {
  _playAfterBuild = false;
  if (!_game) return;
  _game.reset();
  _showLive = false;
  _liveSelection.reset();
  _paused = false;
  _gameFocused = false;
  focusPanel("Scene");
}

void Editor::togglePause() {
  if (playing()) _paused = !_paused;
}

void Editor::stepFrame() {
  if (!playing()) return;
  _paused = true;
  _stepRequested = true;
}

unsigned Editor::advanceGame(int width, int height, float dt) {
  if (!_game) return 0;
  unsigned texture = _game->lastTexture();
  if (!_paused || _stepRequested) {
    texture = _game->frame(width, height, _stepRequested ? 1.0f / 60.0f : dt);
    _stepRequested = false;
  }
  if (!_game->engine().running()) {
    _toasts.show(Toasts::Kind::Info, "The game quit");
    stopPlay();
    return 0;
  }
  return texture;
}

void Editor::setGameFocused(bool focused) {
  _gameFocused = focused;
  if (_game) _game->setFocused(focused);
}

// ---- Misc ------------------------------------------------------------------------

void Editor::focusPanel(const char* name) { _focusRequest = name; }

std::string Editor::assetsFolderForPrefabs() const {
  const std::string folder = assetsFolder();
  if (_project && std::any_of(_project->files().begin(), _project->files().end(), [&](const AssetFile& f) {
        return f.kind == AssetKind::Prefab && fs::path(f.path).parent_path().generic_string() == folder;
      })) {
    return folder;
  }
  return "assets/prefabs";
}

std::string Editor::assetsFolder() const {
  const std::string folder = _assets->folder();
  return folder.ends_with(".atlas.json") ? folder.substr(0, folder.find_last_of('/')) : folder;
}

void Editor::revealAsset(const std::string& path) {
  _assets->reveal(path);
  focusPanel("Assets");
}

void Editor::openPalette(const std::string& prefix) { _palette->open(prefix); }

void Editor::revealInFileManager(const fs::path& path) {
#ifdef __APPLE__
  runDetached("open -R " + quoted(path.string()));
#elif defined(_WIN32)
  runDetached("explorer /select,\"" + path.string() + "\"");
#else
  runDetached("xdg-open " + quoted(path.parent_path().string()));
#endif
}

void Editor::openInCodeEditor(const std::string& path, int line) {
  if (!_project) return;
  const fs::path file = _project->abs(path);
  // VS Code jumps to the line; otherwise the system's default app.
  if (hasProgram("code")) {
    runDetached("code -g " + quoted(file.string() + (line > 0 ? ":" + std::to_string(line) : std::string())));
    return;
  }
#ifdef __APPLE__
  runDetached("open " + quoted(file.string()));
#elif defined(_WIN32)
  runDetached("start \"\" \"" + file.string() + "\"");
#else
  runDetached("xdg-open " + quoted(file.string()));
#endif
}

void Editor::previewSound(const std::string& path) {
  HostedEngine* engine = _preview.engine();
  AudioModule* audio = engine ? engine->engine().getModules().find<AudioModule>() : nullptr;
  if (!audio || !_project) return;
  audio->audio().stopAll();
  if (path.empty()) return;
  if (!audio->audio().knows(path)) audio->audio().registerSound({path}, SoundBuffer::fromFile(_project->abs(path)));
  audio->audio().play(AudioHandle(path));
}
