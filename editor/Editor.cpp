#include "Editor.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <thread>

#include <GLFW/glfw3.h>
#include <nfd.hpp>

#include "Entities.hpp"
#include "Icons.hpp"
#include "LogBook.hpp"
#include "Thumbnails.hpp"
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

// "Sprite", "Sprite 2", ... unused in the scene.
std::string uniqueName(const SceneDocument& doc, const std::string& base) {
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

std::string quoted(const std::string& s) {
  std::string out = "'";
  for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return out + "'";
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
  _game.reset();
  _preview.stop();
}

void Editor::frame(float dt) {
  if (_project) watchFiles();
  autosave();
  if (auto done = _cli.takeFinished()) onBuildFinished(*done);
  _commands.handleShortcuts(gameHasKeyboard());

  if (_project) {
    drawWorkspace(dt);
  } else {
    _welcome->draw(*this);
  }
  _palette->draw(*this);
  _export->draw(*this);
  _settings->draw(*this);
  drawSavePrompt();
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
  if (gameHasKeyboard()) _game->key(key, scancode, action);
}

bool Editor::requestQuit() {
  if (_quitConfirmed) return true;
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
  // Rebuild shortly after changes settle (an editor saving several files at once).
  if (_buildStale && !_cli.busy() && now() - _changeSeen > 0.6) build();
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
  if (!done.ok) {
    _playAfterBuild = false;
    _toasts.show(Toasts::Kind::Error, "Build failed", done.lastLine, "Show Console", [this]() { focusPanel("Console"); });
    return;
  }
  restartPreview();
  Thumbnails::instance().clear();
  if (_playAfterBuild) {
    _playAfterBuild = false;
    startPlay(_playFrom);
  }
}

void Editor::writeThrough(const std::string& path) {
  if (!_project) return;
  std::error_code ec;
  const fs::path to = _project->buildDir() / path;
  if (!fs::exists(_project->buildDir() / ".jm.json", ec)) return;
  fs::create_directories(to.parent_path(), ec);
  fs::copy_file(_project->abs(path), to, fs::copy_options::overwrite_existing, ec);
  if (!ec) _builtFiles[path] = fs::last_write_time(_project->abs(path), ec);
}

void Editor::exportGame(std::vector<std::string> args, std::string outDir) {
  if (!_project) return;
  if (_cli.busy()) {
    _toasts.show(Toasts::Kind::Warning, "A build is running", "Export again once it finishes.");
    return;
  }
  _exportOut = outDir;
  args.insert(args.begin(), "export");
  args.push_back("--out");
  args.push_back(outDir);
  _cli.start(_project->root(), args, "Export");
  focusPanel("Console");
}

// ---- Scenes --------------------------------------------------------------------

void Editor::whenSaved(std::function<void()> then) {
  if (_scene && _scene->dirty()) {
    _afterSave = std::move(then);
    _askSave = true;
    return;
  }
  then();
}

void Editor::openScene(const std::string& path) {
  if (!_project) return;
  if (_scene && _scene->path() == path) return;
  whenSaved([this, path]() {
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

bool Editor::saveScene() {
  if (!_project || !_scene) return false;
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
  }
  if (copied == 0) return;
  _project->rescan();
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
  _paused = false;
  _gameFocused = true;
  focusPanel("Game");
}

void Editor::stopPlay() {
  _playAfterBuild = false;
  if (!_game) return;
  _game.reset();
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
  const char* code = "/usr/local/bin/code";
  if (fs::exists(code) || std::system("command -v code >/dev/null 2>&1") == 0) {
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
