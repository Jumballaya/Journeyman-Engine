#include "Editor.hpp"
#include "JsonFormat.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <thread>

#include "Entities.hpp"
#include "audio/AudioModule.hpp"
#include "audio/SoundBuffer.hpp"
#include "core/app/PlaySession.hpp"
#include "LogBook.hpp"
#include "References.hpp"
#include "TiledFiles.hpp"
#include "physics2d/TransformHierarchy.hpp"
#include "Thumbnails.hpp"
#include "UiThumbnails.hpp"
#include "editors/AssetEditor.hpp"
#include "panels/Panels.hpp"

namespace fs = std::filesystem;

namespace {

constexpr const char* kClipboardKey = "journeymanEntities";

double now() { return ImGui::GetTime(); }

// `path` is `root` itself or lies under the folder `root`.
bool under(const std::string& path, const std::string& root) {
  return path == root || path.starts_with(root + "/");
}

bool hasBuild(const Project& project) {
  std::error_code ec;
  return fs::exists(project.buildDir() / ".jm.json", ec);
}

std::string stemOf(const std::string& path) {
  std::string name = fs::path(path).filename().string();
  for (const char* suffix : {".scene.json", ".prefab.json", ".atlas.json", ".ui.html"}) {
    if (name.ends_with(suffix)) return name.substr(0, name.size() - std::strlen(suffix));
  }
  return fs::path(name).stem().string();
}

// "Sprite", "Sprite 2", ... unused in the scene and in `pending` (entities being
// added). A numbered name counts on from its stem ("Sprite 2" → "Sprite 3").
std::string uniqueName(const SceneDocument& doc, std::string base, const Json& pending = Json::array()) {
  if (const size_t space = base.find_last_of(' '); space != std::string::npos && space + 1 < base.size() &&
      base.find_first_not_of("0123456789", space + 1) == std::string::npos) {
    base.resize(space);
  }
  auto taken = [&](const std::string& name) {
    for (size_t i = 0; i < doc.size(); ++i) {
      if (doc.entity(i).value("name", std::string()) == name) return true;
    }
    return std::any_of(pending.begin(), pending.end(), [&](const Json& e) { return e.is_object() && e.value("name", "") == name; });
  };
  if (!taken(base)) return base;
  for (int n = 2;; ++n) {
    const std::string candidate = base + " " + std::to_string(n);
    if (!taken(candidate)) return candidate;
  }
}

// Whether the prefab at `path` is `target` or holds it among its children, at any depth.
bool prefabHolds(const Project& project, const std::string& path, const std::string& target, int depth = 0) {
  if (path == target) return true;
  const Json* prefab = depth < 16 ? prefabJson(project, path) : nullptr;
  if (!prefab) return false;
  std::function<bool(const Json&)> any = [&](const Json& list) {
    for (const Json& child : list.is_array() ? list : Json::array()) {
      const std::string nested = child.value("prefab", std::string());
      if ((!nested.empty() && prefabHolds(project, nested, target, depth + 1)) || any(child.value("children", Json::array()))) return true;
    }
    return false;
  };
  return any(prefab->value("children", Json::array()));
}

// "Duplicate Player" for one entity, else `many`.
std::string actionLabel(const SceneDocument& doc, const std::vector<EntityUid>& uids, const std::string& verb,
                        const std::string& many) {
  const int index = uids.size() == 1 ? doc.indexOf(uids.front()) : -1;
  return index < 0 ? many : verb + " " + doc.displayName(static_cast<size_t>(index));
}

// A blank sprite: a transform at `position` and an untextured white sprite.
Json spriteComponents(const Json& position) {
  return {{"TransformComponent", {{"position", position}, {"scale", {16, 16}}}},
          {"SpriteComponent", {{"texture", ""}, {"color", {1, 1, 1, 1}}}}};
}

// The component an asset of `kind` is used through, and its field naming the asset.
struct AssetSlot {
  const char* component;
  const char* key;
};

std::optional<AssetSlot> slotFor(AssetKind kind) {
  switch (kind) {
    case AssetKind::Script: return AssetSlot{"ScriptComponent", "script"};
    case AssetKind::Image:
    case AssetKind::Atlas: return AssetSlot{"SpriteComponent", "texture"};
    case AssetKind::Sound: return AssetSlot{"AudioEmitterComponent", "sound"};
    case AssetKind::Ui: return AssetSlot{"UIDocumentComponent", "src"};
    case AssetKind::Map: return AssetSlot{"TileMapComponent", "map"};
    default: return std::nullopt;
  }
}

// {component: {...}} for a UI screen, sound or script naming `path` ("" for none yet).
Json standaloneComponent(AssetKind kind, const std::string& path) {
  const AssetSlot slot = *slotFor(kind);
  Json fields = {{slot.key, path}};
  if (kind == AssetKind::Ui) fields["order"] = 0;
  if (kind == AssetKind::Sound) fields.update({{"gain", 1}, {"looping", false}, {"bus", "sfx"}});
  return {{slot.component, fields}};
}

// Half a picture's pixel size: the scale that shows it 1:1.
std::optional<glm::vec2> pictureScale(const Project& project, const std::string& path) {
  auto picture = Thumbnails::instance().get(project, path);
  if (!picture) return std::nullopt;
  return glm::vec2(picture->size.x, picture->size.y) * 0.5f;
}

// "player" → "assets/prefabs/player.prefab.json", then "..._2", ... free in `folder`.
std::string uniquePrefabPath(const Project& project, const std::string& folder, const std::string& name) {
  const std::string dir = folder.empty() ? "assets/prefabs" : folder;
  for (int n = 1;; ++n) {
    const std::string path = dir + "/" + name + (n == 1 ? "" : "_" + std::to_string(n)) + ".prefab.json";
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

// Opens a file in the system's default app (prefix it to a quoted path).
#ifdef __APPLE__
constexpr const char* kOpenCommand = "open ";
#elif defined(_WIN32)
constexpr const char* kOpenCommand = "start \"\" ";
#else
constexpr const char* kOpenCommand = "xdg-open ";
#endif

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
      _settings(std::make_unique<SettingsDialog>()),
      _sessionDialog(std::make_unique<SessionDialog>()) {
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
  if (auto done = _session.takeFinished(); done && !done->ok && !done->cancelled) {
    consoleError("The multiplayer session stopped with an error", done->lastLine);
  }
  // A preview that couldn't start (it caught a build swapping folders) tries again once a build is there.
  if (_project && !_preview.engine() && !_cli.busy() && now() - _previewRetry > 1.0) {
    _previewRetry = now();
    if (hasBuild(*_project)) _preview.start(*_project);
  }
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
  _sessionDialog->draw(*this);
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

void Editor::consoleError(const std::string& title, const std::string& body) {
  _toasts.show(Toasts::Kind::Error, title, body, "Show Console", [this]() { focusPanel("Console"); });
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

  // Reopen the scene last edited here, else the game's first scene, else any.
  auto exists = [this](const std::string& path) { return !path.empty() && _project->file(path); };
  std::string scene = lastScene(*_project);
  if (!exists(scene)) scene = _project->manifest().value("entryScene", std::string());
  if (const auto scenes = _project->scenes(); !exists(scene) && !scenes.empty()) scene = scenes.front();
  if (!exists(scene)) return true;
  if (auto doc = SceneDocument::load(*_project, scene, error)) {
    setScene(std::move(doc));
    offerRecovery();
  }
  return true;
}

void Editor::closeProject() {
  saveAssets(true);
  UiThumbnails::instance().clear();
  _assetTabs.clear();
  _activeAsset.clear();
  _inspectedAsset.clear();
  stopPlay();
  _preview.stop();
  _prefabReturn.reset();
  setScene(std::nullopt);
  _project.reset();
  _queuedExport = nullptr;
  _builtFiles.clear();
  _buildStale = false;
  Thumbnails::instance().clear();
}

void Editor::restartPreview() {
  if (_project && !_preview.start(*_project)) consoleError("Scene preview unavailable", _preview.error());
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

namespace {
// Files a build reads: everything but folders and half-written saves.
bool isBuildInput(const AssetFile& f) { return f.kind != AssetKind::Folder && !f.path.ends_with(".saving"); }
}  // namespace

void Editor::build() {
  if (!_project || _cli.busy()) return;
  snapshotBuildInputs();
  _cli.start(_project->root(), {"build"}, "Build");
}

void Editor::snapshotBuildInputs() {
  _builtFiles.clear();
  for (const AssetFile& f : _project->files()) {
    if (isBuildInput(f)) _builtFiles[f.path] = f.modified;
  }
  _buildStale = false;
  _changeSeen = 0;
}

void Editor::watchFiles() {
  if (now() - _lastScan < 0.75) return;
  _lastScan = now();
  refreshBuildState();
  // Another program changed the open scene (an agent's edit): its version
  // comes in as an undoable step, so neither side's work is lost.
  if (_scene) {
    if (auto disk = _scene->changedOnDisk(*_project)) {
      const bool overEdits = _scene->dirty();
      _scene->takeDiskVersion(std::move(*disk));
      _toasts.show(overEdits ? Toasts::Kind::Warning : Toasts::Kind::Info, _scene->title() + " changed on disk",
                   overEdits ? "Its new version is loaded; Undo brings back your unsaved edits." : "Reloaded; Undo goes back.");
    }
  }
  // A map changed elsewhere (in Tiled) since the scene saved it: the file wins over the scene's copy.
  if (_scene && !_scene->dirty()) {
    for (const std::string& path : _scene->mapFiles()) {
      const Json* file = parsedFile(path);
      if (file && *file != *_scene->mapFile(path)) _scene->forgetMapFile(path);
    }
  }
  // Rebuild shortly after changes settle (an editor saving several files at once).
  if (_buildStale && !_cli.busy() && now() - _changeSeen > 0.6) build();
}

void Editor::refreshBuildState() {
  if (!_project) return;
  if (!_project->rescan() && !_buildStale) return;
  size_t sources = 0;
  bool stale = false;
  for (const AssetFile& f : _project->files()) {
    if (!isBuildInput(f)) continue;
    ++sources;
    auto it = _builtFiles.find(f.path);
    stale |= it == _builtFiles.end() || it->second != f.modified;
  }
  stale |= sources != _builtFiles.size();
  if (stale && !_buildStale) _changeSeen = now();
  _buildStale = stale;
}

void Editor::onBuildFinished(const CliRunner::Finished& done) {
  auto queuedExport = std::exchange(_queuedExport, nullptr);
  auto queuedSession = std::exchange(_queuedSession, nullptr);
  _buildCancelled = done.cancelled && done.label == "Build";
  if (done.cancelled) {  // asked for: nothing to report, and nothing waits on it
    _playAfterBuild = false;
    return;
  }
  if (done.label == "New Project") {
    if (!done.ok) consoleError("Couldn't create the project", done.lastLine);
  } else if (done.label == "Export") {
    const fs::path out = _project ? _project->root() / _exportOut : fs::path(_exportOut);
    if (done.ok) _toasts.show(Toasts::Kind::Success, "Export complete", done.lastLine, "Reveal", [this, out]() { revealInFileManager(out); });
    else consoleError("Export failed", done.lastLine);
  } else if (!done.ok) {
    _lastBuildFailed = true;
    _playAfterBuild = false;
    queuedExport = nullptr;  // it waited for this build
    queuedSession = nullptr;
    consoleError("Build failed", done.lastLine);
  } else {
    _lastBuildFailed = false;
    _toasts.dismiss("Build failed");  // fixed
    ++_buildGeneration;
    restartPreview();
    Thumbnails::instance().clear();
    if (std::exchange(_playAfterBuild, false)) startPlay(_playFrom);
  }
  if (queuedExport) queuedExport();
  if (queuedSession && done.label == "Build" && done.ok) queuedSession();
}

void Editor::playSession(int players, int latencyMs, float loss) {
  if (!_project || _session.busy()) return;
  refreshBuildState();
  if (_cli.busy() || _buildStale || _lastBuildFailed || !hasBuild(*_project)) {
    _queuedSession = [this, players, latencyMs, loss]() { playSession(players, latencyMs, loss); };
    if (!_cli.busy()) build();
    _toasts.show(Toasts::Kind::Info, "Building first", "The players' windows open when the build is done.");
    return;
  }
  std::vector<std::string> args = {"run", "--peers", std::to_string(players)};
  if (latencyMs > 0) args.insert(args.end(), {"--latency", std::to_string(latencyMs)});
  if (loss > 0.0f) args.insert(args.end(), {"--loss", std::to_string(loss)});
  _session.start(_project->root(), args, "Session");
  focusPanel("Console");
}

void Editor::writeThrough(const std::string& path) {
  // Only files the build copies as they are; scripts compile and atlases and tilesets pack,
  // so those wait for the rebuild the change triggers.
  if (!_project || !hasBuild(*_project) || path.ends_with(".ts") || path.ends_with(".atlas.json") || path.ends_with(".tsj")) return;
  std::error_code ec;
  const fs::path to = _project->buildDir() / path;
  fs::create_directories(to.parent_path(), ec);
  fs::copy_file(_project->abs(path), to, fs::copy_options::overwrite_existing, ec);
  if (!ec) _builtFiles[path] = fs::last_write_time(_project->abs(path), ec);
}

void Editor::exportGame(std::vector<std::string> args, std::string outDir) {
  if (!_project) return;
  if (_cli.busy()) {
    _queuedExport = [this, args, outDir]() { exportGame(args, outDir); };
    _toasts.show(Toasts::Kind::Info, "Export queued", "It starts when the current build finishes.");
    return;
  }
  _exportOut = outDir;
  // A current build needs no rebuilding (nor Node) to export.
  refreshBuildState();
  if (!_buildStale && !_lastBuildFailed && hasBuild(*_project)) args.insert(args.begin(), "--skip-build");
  args.insert(args.begin(), "export");
  args.push_back("--out");
  args.push_back(outDir);
  _cli.start(_project->root(), args, "Export");
  focusPanel("Console");
}

// ---- Scenes --------------------------------------------------------------------

void Editor::setScene(std::optional<SceneDocument> doc) {
  _scene = std::move(doc);
  _selection.clear();
  _hidden.clear();  // uids restart with every document
}

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
  if (auto then = std::exchange(_afterSave, nullptr)) then();
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
    setScene(std::move(doc));
    rememberScene(*_project, path);
    offerRecovery();
    focusPanel("Scene");
  });
}

void Editor::newScene(const std::string& folder) {
  if (!_project) return;
  constexpr const char* kExtension = ".scene.json";
  const std::string dir = under(folder, "scenes") ? folder : "scenes";
  auto pathFor = [this, dir](const std::string& typed) { return freePath(dir, typed, kExtension); };
  prompt("New Scene", "Name", "untitled", [this, pathFor](const std::string& typed) {
    whenSaved([this, path = pathFor(typed)]() {
      setScene(SceneDocument::create(path));
      std::string error;
      if (!_scene->saveAs(*_project, path, error)) {
        _toasts.show(Toasts::Kind::Error, "Couldn't create the scene", error);
        return;
      }
      saveScene();
      rememberScene(*_project, path);
      revealAsset(path);
      focusPanel("Scene");
    });
  }, pathFor);
}

fs::path Editor::recoveryFile() const {
  std::string key = _project->root().string() + "/" + _scene->path();
  std::replace_if(key.begin(), key.end(), [](unsigned char c) { return !std::isalnum(c) && c != '.'; }, '_');
  return settingsDir() / "recovery" / (key + ".json");
}

void Editor::autosave() {
  if (!_project || !_scene || !_scene->dirty() || now() - _lastAutosave < 20.0) return;
  _lastAutosave = now();
  // Atomic: a crash mid-write must not leave an empty file that looks newer than the scene.
  std::string error;
  if (!writeAtomically(recoveryFile(), _scene->serialized(), error)) JM_LOG_WARN("[Editor] autosave: {}", error);
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
                 if (!_scene || recoveryFile() != recovery) return;  // another scene is open now
                 std::ifstream in(recovery, std::ios::binary);
                 const Json restored = Json::parse(in, nullptr, false);
                 if (restored.is_object()) _scene->edit("Restore unsaved changes", [&](Json& doc) { doc.update(restored); });
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
    const std::string folder = fs::path(_scene->path()).parent_path().generic_string();
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
  // A new scene joins the manifest so the game can load it (prefabs are assets, already built).
  Json& scenes = _project->manifest()["scenes"];
  if (!scenes.is_array()) scenes = Json::array();
  if (!_scene->isPrefab() && std::find(scenes.begin(), scenes.end(), Json(_scene->path())) == scenes.end()) {
    scenes.push_back(_scene->path());
    if (!_project->saveManifest(error)) _toasts.show(Toasts::Kind::Error, "Couldn't update .jm.json", error);
  }
  _project->rescan();
  return true;
}

// ---- Assets ---------------------------------------------------------------------

void Editor::inspectAsset(const std::string& path) {
  _inspectedAsset = path;
  _selection.clear();
  _activeAsset.clear();  // the Inspector shows the file, not an asset tab's selection
}

bool Editor::moveAsset(const std::string& from, const std::string& to) {
  if (!_project || from == to) return false;
  const std::string name = fs::path(from).filename().string();
  std::error_code ec;
  if (_project->file(to) || fs::exists(_project->abs(to), ec)) {
    _toasts.show(Toasts::Kind::Warning, "Can't move " + name, to + " already exists.");
    return false;
  }
  fs::create_directories(_project->abs(to).parent_path(), ec);
  fs::rename(_project->abs(from), _project->abs(to), ec);
  if (ec) {
    _toasts.show(Toasts::Kind::Error, "Couldn't move " + name, ec.message());
    return false;
  }
  // The build mirrors source paths: move what it made too, so nothing goes missing before the rebuild.
  if (fs::exists(_project->buildDir() / from, ec)) {
    fs::create_directories((_project->buildDir() / to).parent_path(), ec);
    fs::rename(_project->buildDir() / from, _project->buildDir() / to, ec);
  }
  auto moved = [&](const std::string& path) { return under(path, from) ? to + path.substr(from.size()) : path; };
  // References are quoted paths: "from", "from#region", or "from/..." for folders. Returns how many changed.
  auto rewrite = [&](std::string& text) {
    int count = 0;
    for (const char* tail : {"\"", "#", "/"}) {
      const std::string needle = "\"" + from + tail, replacement = "\"" + to + tail;
      for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + replacement.size())) {
        text.replace(at, needle.size(), replacement);
        ++count;
      }
    }
    return count;
  };
  int references = 0, files = 0;
  _project->rescan();
  for (const AssetFile& f : _project->files()) {
    const std::string ext = fs::path(f.path).extension().string();
    if (f.kind == AssetKind::Folder || (ext != ".json" && ext != ".ts" && ext != ".html" && ext != ".css")) continue;
    std::string text = _project->readText(f.path), error;
    const int count = rewrite(text);
    if (count == 0 || !_project->writeText(f.path, text, error)) continue;
    references += count;
    ++files;
    writeThrough(f.path);
  }
  // The manifest isn't under assets/.
  std::string manifest = _project->manifest().dump(2);
  if (const int count = rewrite(manifest)) {
    if (Json patched = Json::parse(manifest, nullptr, false); !patched.is_discarded()) {
      _project->manifest() = std::move(patched);
      std::string error;
      _project->saveManifest(error);
      references += count;
    }
  }
  // Tiled files name others relative to themselves: rebase those whose file or targets moved.
  for (const AssetFile& f : _project->files()) {
    if (f.kind != AssetKind::Map && f.kind != AssetKind::Tileset) continue;
    const std::string before = under(f.path, to) ? from + f.path.substr(to.size()) : f.path;
    Json file = Json::parse(_project->readText(f.path), nullptr, false);
    if (file.is_discarded() || !tiled::rebase(file, before, f.path, moved)) continue;
    std::string error;
    if (!_project->writeText(f.path, f.kind == AssetKind::Map ? tiled::serializeMap(file) : formatJson(file), error)) continue;
    ++references;
    ++files;
    writeThrough(f.path);
  }
  // Open documents follow: a clean one reloads its rewritten file; a dirty one
  // is patched in place (and saved under its new path if it moved). True if reloaded.
  auto follow = [&](SceneDocument& doc) {
    const std::string path = moved(doc.path());
    std::string error;
    if (!doc.dirty()) {
      auto fresh = SceneDocument::load(*_project, path, error);
      if (fresh) doc = std::move(*fresh);
      return fresh.has_value();
    }
    std::string text = doc.serialized();
    const Json patched = rewrite(text) > 0 ? Json::parse(text, nullptr, false) : Json();
    if (patched.is_object()) doc.edit("Update references", [&](Json& d) { d.update(patched); });
    if (path != doc.path()) doc.saveAs(*_project, path, error);
    return false;
  };
  if (_scene && follow(*_scene)) _selection.clear();
  if (_prefabReturn && follow(*_prefabReturn->scene)) _prefabReturn->selection.clear();
  // Maps being painted move and rebase in the scene, so its next save writes them right.
  if (_scene) {
    for (const std::string& path : _scene->mapFiles()) {
      Json painted = *_scene->mapFile(path);
      const std::string now = moved(path);
      if (!tiled::rebase(painted, path, now, moved) && now == path) continue;
      _scene->edit("Update references", [&](Json& d) {
        d[kMapsKey].erase(path);
        d[kMapsKey][now] = painted;
      });
    }
  }
  for (AssetTab& tab : _assetTabs) {
    const std::string old = tab.doc->path(), now = moved(old);
    const AssetKind tabKind = assetKindOf(now);
    if (Json value = tab.doc->value(); (tabKind == AssetKind::Map || tabKind == AssetKind::Tileset) && tiled::rebase(value, old, now, moved)) {
      tab.doc->edit("Update references", [&](Json& v) { v = value; });
    }
    if (now != old) tab.doc->movedTo(now);
  }
  _activeAsset = moved(_activeAsset);
  _inspectedAsset = moved(_inspectedAsset);
  _project->rescan();
  _preview.invalidate();
  if (references > 0) {
    _toasts.show(Toasts::Kind::Success, "Moved " + name,
                 "Updated " + std::to_string(references) + (references == 1 ? " reference" : " references") + " in " +
                     std::to_string(files + 1) + (files == 0 ? " file" : " files") + ".");
  }
  return true;
}

void Editor::deleteAsset(const std::string& path) {
  if (!_project) return;
  const std::string name = fs::path(path).filename().string();
  const auto users = referencesTo(*_project, path);  // before it's gone: what still names it
  std::error_code ec;
  const fs::path trash = settingsDir() / "trash" / _project->root().filename() / path;
  fs::create_directories(trash.parent_path(), ec);
  fs::remove_all(trash, ec);
  fs::rename(_project->abs(path), trash, ec);
  if (ec) {
    _toasts.show(Toasts::Kind::Error, "Couldn't delete " + name, ec.message());
    return;
  }
  // Deleted scenes leave the manifest, so the build keeps working.
  std::vector<Json> unlisted;
  if (Json& scenes = _project->manifest()["scenes"]; scenes.is_array()) {
    auto gone = [&](const Json& s) { return s.is_string() && under(s.get<std::string>(), path); };
    std::copy_if(scenes.begin(), scenes.end(), std::back_inserter(unlisted), gone);
    scenes.erase(std::remove_if(scenes.begin(), scenes.end(), gone), scenes.end());
  }
  std::string error;
  if (!unlisted.empty()) _project->saveManifest(error);
  if (_scene && under(_scene->path(), path)) setScene(std::nullopt);
  if (under(_inspectedAsset, path)) _inspectedAsset.clear();
  _project->rescan();
  // Recoverable until the same path is deleted again.
  std::string usedIn;
  for (const std::string& u : users) {
    if (u != ".jm.json") usedIn += (usedIn.empty() ? "" : ", ") + fs::path(u).filename().string();
  }
  _toasts.show(usedIn.empty() ? Toasts::Kind::Info : Toasts::Kind::Warning, "Deleted " + name,
               usedIn.empty() ? "" : "Still named in " + usedIn, "Undo", [this, root = _project->root(), path, trash, unlisted]() {
    if (!_project || _project->root() != root) return;  // that project was closed
    std::error_code undoError;
    fs::rename(trash, _project->abs(path), undoError);
    if (!unlisted.empty()) {
      for (const Json& scene : unlisted) _project->manifest()["scenes"].push_back(scene);
      std::string error;
      _project->saveManifest(error);
    }
    _project->rescan();
  });
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
      continue;
    }
    for (const auto& entry : fs::recursive_directory_iterator(target, ec)) {
      if (entry.is_regular_file()) addedFile(fs::relative(entry.path(), _project->root()).generic_string());
    }
  }
  if (copied == 0) return;
  refreshBuildState();
  _toasts.show(Toasts::Kind::Success, copied == 1 ? "Imported " + fs::path(last).filename().string() : "Imported " + std::to_string(copied) + " files",
               "Into " + into + "/", "Show", [this, last]() { revealAsset(last); });
}

// ---- Prefabs --------------------------------------------------------------------

bool Editor::writePrefab(const std::string& path, Json prefab, const std::string& failure) {
  wholeNumbersAsIntegers(prefab);
  std::string error;
  if (_project->writeText(path, formatJson(prefab), error)) return true;
  _toasts.show(Toasts::Kind::Error, failure, error);
  return false;
}

std::string Editor::createPrefab(EntityUid uid, const std::string& folder, std::string name) {
  if (!_project || !_scene || _scene->isPrefab()) return {};
  const int index = _scene->indexOf(uid);
  if (index < 0) return {};
  const Json entity = _scene->entity(static_cast<size_t>(index));
  if (name.empty()) name = _scene->displayName(static_cast<size_t>(index));
  const std::string path = uniquePrefabPath(*_project, folder, safeName(name));

  // The prefab keeps everything but where this one stands; the instance keeps that.
  Json components = effectiveComponents(*_project, entity);
  Json position;
  if (components.contains("TransformComponent") && components["TransformComponent"].contains("position")) {
    position = components["TransformComponent"]["position"];
    components["TransformComponent"]["position"] = Json::array({0, 0, position.size() > 2 ? position[2] : Json(0)});
  }
  // Its children become the prefab's, placed as they are around it.
  Json file = {{"components", components}, {"tags", Json::array()}};
  if (entity.contains("children")) {
    std::function<void(Json&)> clean = [&](Json& list) {
      for (Json& child : list) {
        child.erase(kUidKey);
        if (child.contains("children")) clean(child["children"]);
      }
    };
    file["children"] = entity["children"];
    clean(file["children"]);
  }
  if (!writePrefab(path, file, "Couldn't save the prefab")) return {};
  addedFile(path);
  _scene->editEntity(uid, "Make prefab " + fs::path(path).stem().stem().string(), [&](Json& e) {
    e.erase("components");
    e.erase("overrides");
    e.erase("children");
    e["prefab"] = path;
    if (!position.is_null()) e["overrides"] = {{"TransformComponent", {{"position", position}}}};
  });
  _toasts.show(Toasts::Kind::Success, "Made a prefab", path + "\nDrag it into scenes to place more.", "Show",
               [this, path]() { revealAsset(path); });
  return path;
}

void Editor::newPrefab(const std::string& folder) {
  if (!_project) return;
  const std::string path = uniquePrefabPath(*_project, folder, "new_prefab");
  if (!writePrefab(path, {{"components", spriteComponents({0, 0, 0})}, {"tags", Json::array()}}, "Couldn't create the prefab")) return;
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
  const Json overrides = entity.value("overrides", Json::object());  // named: items() of a temporary dangles
  for (const auto& [name, fields] : overrides.items()) {
    if (name == "tags" || name == "children" || !fields.is_object() || (!component.empty() && name != component)) continue;
    Json moving = fields;
    if (component.empty() && name == "TransformComponent") moving.erase("position");
    if (!moving.empty()) applied[name] = moving;
  }
  if (applied.empty()) return;
  Json& components = prefab["components"];
  for (const auto& [name, fields] : applied.items()) {
    components[name] = components.contains(name) ? mergeDeep(components[name], fields) : fields;
  }
  if (!writePrefab(path, prefab, "Couldn't save the prefab")) return;
  writeThrough(path);
  const std::string file = fs::path(path).filename().string();
  const std::string label = "Apply to " + file;
  _scene->editEntity(uid, label, [&](Json& e) {
    Json& overrides = e["overrides"];
    for (const auto& [name, fields] : applied.items()) {
      Json& left = overrides[name];
      for (const auto& [key, _] : fields.items()) left.erase(key);
      if (left.empty()) overrides.erase(name);
    }
    if (overrides.empty()) e.erase("overrides");
  });
  _preview.invalidate();  // every instance changes
  _toasts.show(Toasts::Kind::Success, "Applied to " + file, "Every instance has the change now.", "Undo",
               [this, root = _project->root(), path, before, label]() {
                 if (!_project || _project->root() != root) return;  // that project was closed
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
      return;
    }
    e["overrides"].erase(component);
    if (e["overrides"].empty()) e.erase("overrides");
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
  _prefabReturn = PrefabReturn{std::make_unique<SceneDocument>(std::move(*_scene)), _selection, _hidden};
  setScene(std::move(doc));
  if (_scene->size() > 0) _selection.push_back(_scene->uid(0));  // the prefab's root, ready to edit
  focusPanel("Scene");
}

void Editor::leavePrefab() {
  if (!_prefabReturn) return;
  _scene = std::move(*_prefabReturn->scene);
  _selection = std::move(_prefabReturn->selection);
  _hidden = std::move(_prefabReturn->hidden);
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

// ---- Selection and entities -----------------------------------------------------

bool Editor::isSelected(EntityUid uid) const {
  return std::find(_selection.begin(), _selection.end(), uid) != _selection.end();
}

void Editor::select(EntityUid uid, SelectMode mode) {
  _inspectedAsset.clear();
  const auto it = std::find(_selection.begin(), _selection.end(), uid);
  if (mode == SelectMode::Replace) {
    _selection = {uid};
  } else if (it == _selection.end()) {
    _selection.insert(_selection.begin(), uid);
  } else if (mode == SelectMode::Toggle) {
    _selection.erase(it);
  }
}

void Editor::selectAll(std::vector<EntityUid> uids) {
  _inspectedAsset.clear();
  _selection = std::move(uids);
}

// The selection without entities inside other selected ones (they come along with them).
std::vector<EntityUid> Editor::selectionRoots() const {
  std::vector<EntityUid> out;
  for (EntityUid uid : _selection) {
    const bool carried = std::any_of(_selection.begin(), _selection.end(), [&](EntityUid other) { return _scene->isInside(uid, other); });
    if (!carried) out.push_back(uid);
  }
  return out;
}

void Editor::duplicateSelection() {
  if (!_scene || _selection.empty()) return;
  const auto copies = _scene->duplicate(selectionRoots(), actionLabel(*_scene, _selection, "Duplicate", "Duplicate"), [&](Json& copy) {
    if (copy.contains("name")) copy["name"] = uniqueName(*_scene, copy["name"].get<std::string>());
  });
  if (!copies.empty()) _selection = copies;
}

void Editor::copySelection() {
  if (!_scene || _selection.empty()) return;
  Json list = Json::array();
  for (size_t i = 0; i < _scene->size(); ++i) {
    const auto roots = selectionRoots();
    if (std::find(roots.begin(), roots.end(), _scene->uid(i)) == roots.end()) continue;
    if (_scene->isPrefab() && i == 0) continue;  // the prefab itself isn't a part of it
    list.push_back(_scene->entity(i));  // uids are dropped when pasted
  }
  ImGui::SetClipboardText(Json{{kClipboardKey, list}}.dump(2).c_str());
}

void Editor::paste() {
  if (!_scene) return;
  const char* text = ImGui::GetClipboardText();
  const Json clip = Json::parse(text ? text : "", nullptr, false);
  if (!clip.is_object() || !clip.value(kClipboardKey, Json()).is_array()) {
    _toasts.show(Toasts::Kind::Info, "Nothing to paste", "Copy entities first.");
    return;
  }
  std::vector<Json> entries;
  Json pending = Json::array();
  for (Json e : clip[kClipboardKey]) {
    if (!e.is_object()) continue;
    if (e.contains("name")) e["name"] = uniqueName(*_scene, e["name"].get<std::string>(), pending);
    pending.push_back(e);
    entries.push_back(std::move(e));
  }
  const auto added = _scene->addEntities(std::move(entries), "Paste");
  if (!added.empty()) _selection = added;
}

void Editor::deleteSelection() {
  if (!_scene || _selection.empty()) return;
  _scene->removeEntities(_selection, actionLabel(*_scene, _selection, "Delete", "Delete Entities"));
  std::erase_if(_selection, [this](EntityUid uid) { return _scene->indexOf(uid) < 0; });
}

std::optional<TransformComponent> Editor::worldTransform(EntityUid uid) const {
  if (uid == 0) return std::nullopt;
  return _preview.transformOf(uid);
}

void Editor::reparent(const std::vector<EntityUid>& uids, EntityUid parent, int at) {
  if (!_scene || uids.empty()) return;
  if (parent == 0) parent = newEntityParent();  // a prefab's top level is its root's inside
  const auto parentWorld = worldTransform(parent);
  std::map<EntityUid, TransformComponent> worlds;
  for (EntityUid uid : uids) {
    if (auto t = worldTransform(uid)) worlds[uid] = *t;
  }
  const std::string what = uids.size() == 1 && _scene->indexOf(uids.front()) >= 0
                               ? _scene->displayName(static_cast<size_t>(_scene->indexOf(uids.front())))
                               : "Entities";
  std::string label = "Move " + what;
  if (parent && _scene->indexOf(parent) >= 0) label += " into " + _scene->displayName(static_cast<size_t>(_scene->indexOf(parent)));
  const bool moved = _scene->moveEntities(uids, parent, at, label, [&](Json& e) {
    if (parent) e.erase("group");  // a child spawns with its parent
    // It stays where it stands: its transform becomes relative to the new parent.
    auto world = worlds.find(e.value(kUidKey, EntityUid{0}));
    if (world == worlds.end() || (parent && !parentWorld)) return;
    const TransformComponent& w = world->second;
    glm::vec3 position = w.position;
    float rotation = w.rotationRad;
    if (parentWorld) {
      const LocalTransformComponent local = localTo(*parentWorld, w);
      position = local.position;
      rotation = local.rotationRad;
    }
    auto round = [](float v) { return std::round(v * 1000.0f) / 1000.0f; };
    Json& transform = editableComponent(e, "TransformComponent");
    transform["position"] = {round(position.x), round(position.y), round(position.z)};
    if (std::abs(rotation) > 1e-6f || transform.contains("rotation")) transform["rotation"] = round(rotation);
  });
  if (!moved) _toasts.show(Toasts::Kind::Info, "Can't move it there", "An entity can't go inside itself, and a prefab's root stays the root.");
}

EntityUid Editor::newEntityParent() const {
  return _scene && _scene->isPrefab() && _scene->size() > 0 ? _scene->uid(0) : 0;  // a prefab grows from its root
}

Json Editor::localPosition(glm::vec2 world) const {
  glm::vec3 at(std::round(world.x), std::round(world.y), 0.0f);
  if (auto parent = worldTransform(newEntityParent())) {
    TransformComponent here;
    here.position = at;
    at = glm::round(localTo(*parent, here).position);
  }
  return Json::array({at.x, at.y, at.z});
}

EntityUid Editor::createChild(EntityUid parent) {
  if (!_scene || _scene->indexOf(parent) < 0) return 0;
  const Json entry = {{"name", uniqueName(*_scene, "Entity")}, {"components", {{"TransformComponent", {{"position", {0, 0, 0}}}}}}};
  const EntityUid uid = _scene->addEntity(entry, "Create Child", parent);
  _selection = {uid};
  return uid;
}

EntityUid Editor::createEntity(const std::string& kind, glm::vec2 at) {
  if (!_scene) return 0;
  const Json position = localPosition(at);
  Json components = {{"TransformComponent", {{"position", position}}}};
  std::string name = kind;
  if (kind == "Empty") {
    name = "Entity";
  } else if (kind == "Sprite") {
    components = spriteComponents(position);
  } else if (kind == "Text") {
    components["TextComponent"] = {{"text", "Text"}, {"size", 16}, {"color", {1, 1, 1, 1}}};
  } else if (kind == "Tile Map") {
    components["TileMapComponent"] = {{"map", ""}};
  } else if (kind == "UI Screen" || kind == "Sound" || kind == "Script") {
    components = standaloneComponent(kind == "Sound" ? AssetKind::Sound : kind == "Script" ? AssetKind::Script : AssetKind::Ui, "");
  } else {
    components = Json::object();
  }
  const EntityUid uid = _scene->addEntity({{"name", uniqueName(*_scene, name)}, {"components", components}},
                                          "Create " + name, newEntityParent());
  _selection = {uid};
  if (kind == "Tile Map") {  // its map file, named now
    newAsset("map", {}, [this, uid](const std::string& path) {
      if (_scene && _scene->find(uid)) {
        _scene->editEntity(uid, "Set Tile Map", [&](Json& e) { e["components"]["TileMapComponent"]["map"] = path; });
      }
    });
  }
  return uid;
}

EntityUid Editor::instantiateAsset(const std::string& path, glm::vec2 at) {
  if (!_scene || !_project) return 0;
  const size_t hash = path.find('#');
  const AssetKind kind = assetKindOf(path.substr(0, hash));
  if (kind == AssetKind::Scene) {
    openScene(path);
    return 0;
  }
  if (kind == AssetKind::Prefab && _scene->isPrefab() && prefabHolds(*_project, path, _scene->path())) {
    _toasts.show(Toasts::Kind::Info, "A prefab can't hold itself", stemOf(path) + " is (or holds) the prefab being edited.");
    return 0;
  }
  const Json position = localPosition(at);
  Json entry = {{"name", uniqueName(*_scene, stemOf(hash == std::string::npos ? path : path.substr(hash + 1)))}};
  std::string label = "Add " + entry["name"].get<std::string>();
  Json components = Json::object();
  switch (kind) {
    case AssetKind::Prefab: {
      entry["prefab"] = path;
      label = "Add " + stemOf(path);
      const Json* prefab = prefabJson(*_project, path);
      if (prefab && prefab->value("components", Json::object()).contains("TransformComponent")) {
        const Json base = (*prefab)["components"]["TransformComponent"].value("position", Json::array({0, 0, 0}));
        entry["overrides"] = {{"TransformComponent", {{"position", {position[0], position[1], base.size() > 2 ? base[2] : Json(0)}}}}};
      }
      break;
    }
    case AssetKind::Image:
    case AssetKind::Atlas: {
      const glm::vec2 half = pictureScale(*_project, path).value_or(glm::vec2(16.0f));
      components["TransformComponent"] = {{"position", position}, {"scale", {half.x, half.y}}};
      components["SpriteComponent"] = {{"texture", path}};
      break;
    }
    case AssetKind::Map:
      components["TransformComponent"] = {{"position", position}};
      components["TileMapComponent"] = {{"map", path}};
      break;
    case AssetKind::Tileset: {
      // A new map painting with it.
      newAsset("map", fs::path(path).parent_path().generic_string(), [this, path, position](const std::string& made) {
        editMapFile(made, [&](Json& m) {
          tiled::addTileset(m, made, path, [&](const std::string& p) { return tileset(p) ? tiled::tileSpan(*tileset(p)) : 0u; });
        });
        if (!_scene || _scene->isPrefab()) return;
        const Json entry = {{"name", uniqueName(*_scene, stemOf(made))},
                            {"components", {{"TransformComponent", {{"position", position}}}, {"TileMapComponent", {{"map", made}}}}}};
        _selection = {_scene->addEntity(entry, "Add " + stemOf(made))};
      });
      return 0;
    }
    case AssetKind::Ui:
    case AssetKind::Sound:
    case AssetKind::Script:
      components = standaloneComponent(kind, path);
      break;
    default:
      _toasts.show(Toasts::Kind::Info, "Can't place this file", path + " isn't something a scene can hold.");
      return 0;
  }
  if (kind != AssetKind::Prefab) entry["components"] = components;
  _selection = {_scene->addEntity(entry, label, newEntityParent())};
  return _selection.front();
}

bool Editor::applyAssetToEntity(EntityUid uid, const std::string& path, bool dryRun) {
  if (!_scene || !_project || !_scene->find(uid)) return false;
  const AssetKind kind = assetKindOf(path.substr(0, path.find('#')));
  if (kind == AssetKind::Tileset) {  // onto a tile map: its map can paint with it
    const std::string mapPath = mapPathOf(uid);
    if (mapPath.empty() || !map(mapPath)) return false;
    if (dryRun) return true;
    editMap(mapPath, "Add Tileset " + stemOf(path), [&](Json& m) {
      tiled::addTileset(m, mapPath, path, [&](const std::string& p) { return tileset(p) ? tiled::tileSpan(*tileset(p)) : 0u; });
    });
    _selection = {uid};
    return true;
  }
  const std::optional<AssetSlot> slot = slotFor(kind);
  if (!slot) return false;
  if (kind == AssetKind::Atlas && path.find('#') == std::string::npos) return false;  // a whole atlas isn't a picture
  if (dryRun) return true;
  const std::string name = _scene->displayName(static_cast<size_t>(_scene->indexOf(uid)));
  _scene->editEntity(uid, "Set " + componentLabel(slot->component) + " of " + name, [&](Json& e) {
    editableComponent(e, slot->component)[slot->key] = path;
    if (kind != AssetKind::Image && kind != AssetKind::Atlas) return;
    // A new sprite takes the picture's size.
    const Json scale = fieldValue(*_project, e, "TransformComponent", "scale");
    const bool unsized = !scale.is_array() || (scale.size() >= 2 && scale[0] == 1 && scale[1] == 1);
    if (auto half = pictureScale(*_project, path); half && unsized) {
      editableComponent(e, "TransformComponent")["scale"] = {half->x, half->y};
    }
  });
  _selection = {uid};
  return true;
}

Json Editor::resolveForEngine(const Json& entity) {
  Json out = {{"name", entity.value("name", std::string())}, {kUidKey, entity.value(kUidKey, EntityUid{0})}};
  Json components = _project ? effectiveComponents(*_project, entity) : entity.value("components", Json::object());
  // A tile map as edited, with its tilesets' sources: the preview shows unsaved and unbuilt changes.
  if (auto it = components.find("TileMapComponent"); it != components.end() && it->value("map", Json()).is_string()) {
    const std::string path = (*it)["map"];
    if (const Json* m = path.empty() ? nullptr : map(path)) {
      Json tilesets = Json::object();
      for (const tiled::TilesetRef& ref : tiled::tilesets(*m, path)) {
        const Json* set = ref.path.empty() ? nullptr : tileset(ref.path);
        if (!set) continue;
        tilesets[ref.path] = *set;
        for (const std::string& image : tiled::references(*set, ref.path)) mirrorToBuild(image);
      }
      for (const std::string& image : tiled::references(*m, path)) {
        if (assetKindOf(image) == AssetKind::Image) mirrorToBuild(image);
      }
      *it = {{"map", *m}, {"mapPath", path}, {"tilesets", std::move(tilesets)}};
    }
  }
  out["components"] = std::move(components);
  // An instance shows the children its prefab brings (the scene's own are spawned one by one).
  if (Json children = _project ? prefabChildren(*_project, entity) : Json::array(); !children.empty()) out["children"] = std::move(children);
  return out;
}

// ---- Tile maps --------------------------------------------------------------------

std::string Editor::mapPathOf(EntityUid uid) {
  const Json* entity = _scene && _project ? _scene->find(uid) : nullptr;
  if (!entity) return "";
  const Json source = effectiveComponents(*_project, *entity).value("TileMapComponent", Json::object()).value("map", Json());
  return source.is_string() ? source.get<std::string>() : "";
}

const Json* Editor::parsedFile(const std::string& path) {
  const AssetFile* file = _project ? _project->file(path) : nullptr;
  if (!file) return nullptr;
  auto& [time, json] = _parsed[path];
  if (time != file->modified || json.is_null()) {
    time = file->modified;
    json = Json::parse(_project->readText(path), nullptr, false);
    if (json.is_discarded()) json = nullptr;
  }
  return json.is_null() ? nullptr : &json;
}

const Json* Editor::map(const std::string& path) {
  if (const Json* painted = _scene ? _scene->mapFile(path) : nullptr) return painted;
  return parsedFile(path);
}

void Editor::editMap(const std::string& path, const std::string& label, const std::function<void(Json&)>& mutate,
                     const std::string& mergeKey) {
  const Json* current = _scene ? map(path) : nullptr;
  if (!current) return;
  Json next = *current;
  mutate(next);
  if (next == *current) return;
  _scene->edit(label, [&](Json& doc) { doc[kMapsKey][path] = std::move(next); }, mergeKey);
}

void Editor::editMapFile(const std::string& path, const std::function<void(Json&)>& mutate) {
  const Json* current = parsedFile(path);
  if (!current) return;
  Json next = *current;
  mutate(next);
  std::string error;
  if (_project->writeText(path, tiled::serializeMap(next), error)) writeThrough(path);
  _project->rescan();
}

std::string Editor::newMapText(const std::string& path) {
  Json m = tiled::newMap({20, 15}, {16, 16});
  auto first = std::find_if(_project->files().begin(), _project->files().end(), [](const AssetFile& f) { return f.kind == AssetKind::Tileset; });
  if (const Json* set = first == _project->files().end() ? nullptr : tileset(first->path)) {
    m["tilewidth"] = set->value("tilewidth", 16);
    m["tileheight"] = set->value("tileheight", 16);
    tiled::addTileset(m, path, first->path, [](const std::string&) { return 0u; });
  }
  return tiled::serializeMap(m);
}

void Editor::mirrorToBuild(const std::string& path) {
  if (!_project) return;
  std::error_code ec;
  const fs::path built = _project->buildDir() / path;
  if (!fs::exists(built, ec) || fs::last_write_time(built, ec) < fs::last_write_time(_project->abs(path), ec)) writeThrough(path);
}

void Editor::openInTiled(const std::string& path) {
  if (!_project) return;
  auto open = [this, path]() {
#ifdef __APPLE__
    const bool installed = fs::exists("/Applications/Tiled.app") || hasProgram("tiled");
    const std::string command = "open -a Tiled " + quoted(_project->abs(path).string());
#else
    const bool installed = hasProgram("tiled");
    const std::string command = "tiled " + quoted(_project->abs(path).string());
#endif
    if (!installed) {
      _toasts.show(Toasts::Kind::Info, "Tiled isn't installed", "Get it free from mapeditor.org; maps and tilesets open in it as they are.");
      return;
    }
    runDetached(command);
  };
  // Tiled reads the file: the map being painted is saved first.
  if (_scene && _scene->mapFile(path) && _scene->dirty()) whenCurrentSaved(open);
  else open();
}

// ---- Play -------------------------------------------------------------------------

void Editor::playSceneFile() {
  // The game sees the scene as edited, saved or not: write it (and any painted
  // maps) into build/, where the running game reads files from.
  auto write = [this](const std::string& path, const std::string& text) {
    std::string error;
    if (!writeAtomically(_project->buildDir() / path, text, error)) JM_LOG_WARN("[Editor] play: {}", error);
  };
  write(_scene->path(), _scene->serialized());
  for (const std::string& map : _scene->mapFiles()) write(map, tiled::serializeMap(*_scene->mapFile(map)));
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
  if (_cli.busy() || _buildStale || !hasBuild(*_project)) {
    _playAfterBuild = true;
    _playFrom = from;
    build();
    focusPanel("Game");
    return;
  }
  if (_scene && !_scene->isPrefab()) playSceneFile();
  // Recorded as jm run records a play: the person's agent can see it (jm plays).
  // Watching: the editor rebuilds on every save, and the running game picks the changes up.
  const HostedEngine::Options options{true, from == PlayFrom::Game ? first : _scene->path(),
                                      settingsDir() / "play-saves" / _project->root().filename(),
                                      session::newPlayDir(_project->root()), true};
  std::string error;
  _game = HostedEngine::create(_project->buildDir(), options, error);
  if (!_game) {
    consoleError("The game didn't start", error);
    return;
  }
  if (_clearConsoleOnPlay) LogBook::instance().clear();
  _showLive = _gameFocused = true;
  _liveSelection.reset();
  _paused = false;
  focusPanel("Game");
}

void Editor::stopPlay() {
  _playAfterBuild = false;
  if (!_game) return;
  _game.reset();
  _showLive = _gameFocused = _paused = false;
  _liveSelection.reset();
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
  // A marker (F8) says so where the person is looking.
  if (std::string notice = _game->engine().notice(); notice != _gameNotice) {
    _gameNotice = notice;
    if (!notice.empty()) _toasts.show(Toasts::Kind::Info, "Play: " + notice, "Your agent can look at it: jm plays show");
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
  const bool holdsPrefabs = _project && std::any_of(_project->files().begin(), _project->files().end(), [&](const AssetFile& f) {
    return f.kind == AssetKind::Prefab && fs::path(f.path).parent_path().generic_string() == folder;
  });
  return holdsPrefabs ? folder : "assets/prefabs";
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
  runDetached(kOpenCommand + quoted(file.string()));
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
