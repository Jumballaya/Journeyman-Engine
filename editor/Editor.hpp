#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "AssetDocument.hpp"
#include "CliRunner.hpp"
#include "Commands.hpp"
#include "HostedEngine.hpp"
#include "Preview.hpp"
#include "Project.hpp"
#include "SceneDocument.hpp"
#include "Toasts.hpp"

class HierarchyPanel;
class InspectorPanel;
class ScenePanel;
class GamePanel;
class AssetsPanel;
class ConsolePanel;
class CommandPalette;
class WelcomeScreen;
class ExportDialog;
class SettingsDialog;
class AssetEditor;

// Scene view tools; the tile tools apply to a selected tile map.
enum class Tool { Select, Move, Rotate, Scale, Pan, TileBrush, TileRect, TileFill, TileErase, TilePick };
bool isTileTool(Tool tool);

// The editor: an open project, the scene being edited, selection, the
// preview and play sessions, builds, and the panels that show them. Panels
// read state and act through these methods, so every action works the same
// from a menu, a shortcut, the palette or a drag.
class Editor {
 public:
  Editor();
  ~Editor();

  // One frame of UI, between ImGui::NewFrame and ImGui::Render.
  void frame(float dt);
  // "Super Pip - level - Journeyman", with a dot when unsaved.
  std::string windowTitle() const;
  // GLFW key events; the running game gets them while its view has focus.
  void onKey(int key, int scancode, int action);
  // Asks to quit; returns true once nothing unsaved is in the way.
  bool requestQuit();
  bool quitConfirmed() const { return _quitConfirmed; }
  // Whether frames must come as fast as the display allows (a running game,
  // a build's progress, an easing camera); otherwise the editor can idle.
  bool busy() const;

  // Project.
  bool openProject(const std::filesystem::path& folder);
  void closeProject();
  Project* project() { return _project ? &*_project : nullptr; }

  // Scene documents.
  SceneDocument* scene() { return _scene ? &*_scene : nullptr; }
  void openScene(const std::string& path);
  void newScene();
  bool saveScene();
  void saveSceneAs();
  // Asks for a name in a small dialog; `done` gets it (trimmed, non-empty).
  void prompt(std::string title, std::string label, std::string initial, std::function<void(const std::string&)> done);
  // Runs `then` once unsaved changes are saved or discarded (asks first).
  void whenSaved(std::function<void()> then);

  // Selection (scene entity uids; the first is the primary).
  const std::vector<EntityUid>& selection() const { return _selection; }
  bool isSelected(EntityUid uid) const;
  enum class SelectMode { Replace, Add, Toggle };
  void select(EntityUid uid, SelectMode mode = SelectMode::Replace);
  void selectAll(std::vector<EntityUid> uids);
  void clearSelection() { _selection.clear(); }
  EntityUid primary() const { return _selection.empty() ? 0 : _selection.front(); }
  // A project file shown in the Inspector instead of entities (the last thing clicked wins).
  void inspectAsset(const std::string& path);
  const std::string& inspectedAsset() const { return _inspectedAsset; }
  // Renames or moves a project file or folder, rewriting every reference to
  // it (scenes, prefabs, scripts, UI, the manifest) and the open scene.
  bool moveAsset(const std::string& from, const std::string& to);
  // Moves a file to the editor's trash (with Undo), dropping a scene from the manifest.
  void deleteAsset(const std::string& path);
  // Asset editors: files with a dedicated editor (tilesets, input bindings,
  // atlases, data...) open as tabs beside the Scene and Game views. Their
  // edits save themselves once they settle.
  static bool hasAssetEditor(const std::string& path);
  // Opens (or focuses) its tab; anything without an editor opens in the code editor.
  void openAsset(const std::string& path);
  // The document of the asset tab in use (Undo goes there), or null for the scene.
  AssetDocument* activeAsset();
  // Runs (or with `run` false, checks) an Edit command in the active asset
  // tab; false when it doesn't take it, so the scene gets it.
  bool assetCommand(const std::string& id, bool run);
  // Draws the active asset tab's Inspector content; false if it has none.
  bool drawAssetInspector();
  // Prefabs.
  // Saves an entity as a new prefab in `folder` (named after it unless `name`
  // is given; made unique) and makes it an instance. Returns the path, or "".
  std::string createPrefab(EntityUid uid, const std::string& folder, std::string name = {});
  // A new prefab (a transform and a sprite) in `folder`, opened for editing.
  void newPrefab(const std::string& folder);
  // Writes an instance's overrides (one component's, or all but its position)
  // into its prefab, so every instance gets them; the instance then matches.
  void applyOverrides(EntityUid uid, const std::string& component = {});
  void revertOverrides(EntityUid uid, const std::string& component = {});
  // Opens a prefab for editing; the scene it came from is one click away.
  void editPrefab(const std::string& path);
  std::string returnScene() const { return _prefabReturn ? _prefabReturn->scene->path() : std::string(); }
  void returnFromPrefab();
  // Instances of a prefab in the open scene.
  std::vector<EntityUid> instancesOf(const std::string& prefab) const;
  // A new file of `kind` ("script", "effect", "transition", "stylesheet",
  // "tileset", "atlas", "data", "input", "ui") in `folder`, from a template,
  // after asking for its name; then opened in its editor.
  void newAsset(const std::string& kind, const std::string& folder);
  // A file just written into the project: listed in the manifest if it needs
  // to be (so builds take it), copied into build/, and seen by the Assets panel.
  void addedFile(const std::string& path);
  // Copies files dropped from the OS into the project (into `folder`).
  void importFiles(const std::vector<std::filesystem::path>& files, const std::string& folder);

  // Entity actions on the selection or at a world point.
  void duplicateSelection();
  void deleteSelection();
  // The selection as JSON on the system clipboard; paste adds it to the open scene.
  void copySelection();
  void paste();
  EntityUid createEntity(const std::string& kind, glm::vec2 at);
  // Drops an asset into the scene: a prefab instance, a sprite, a tile map, a UI screen...
  EntityUid instantiateAsset(const std::string& path, glm::vec2 at);
  // Hidden in the Scene view only (an editing aid; the game and saves are unaffected).
  bool hiddenInView(EntityUid uid) const { return _hidden.contains(uid); }
  void toggleHiddenInView(EntityUid uid) { _hidden.contains(uid) ? (void)_hidden.erase(uid) : (void)_hidden.insert(uid); }
  // Clear the console when play starts.
  bool& clearConsoleOnPlay() { return _clearConsoleOnPlay; }
  // Drops an asset onto an entity: a script, image, sound, UI document or
  // tileset sets (or adds) the matching component. False if it doesn't apply.
  bool applyAssetToEntity(EntityUid uid, const std::string& path, bool dryRun = false);
  // A scene entry as the engine should spawn it: prefab merged, maps being
  // painted inlined, editor ids kept.
  Json resolveForEngine(const Json& entity);

  // Tile maps being painted: rows of a .txt map file (or an inline map),
  // edited through the scene document so painting undoes with everything else.
  std::vector<std::string> mapRows(EntityUid uid);
  void setMapRows(EntityUid uid, std::vector<std::string> rows, const std::string& label, const std::string& mergeKey);
  // The tile characters of the selected map's tileset, with their images.
  char brushTile() const { return _brushTile; }
  void setBrushTile(char c) { _brushTile = c; }

  // Preview and play.
  Preview& preview() { return _preview; }
  bool playing() const { return _game != nullptr; }
  bool paused() const { return _paused; }
  // Scene: the open scene, as edited. Game: from the project's first scene,
  // with the open scene's unsaved edits included when the game reaches it.
  enum class PlayFrom { Scene, Game };
  void startPlay(PlayFrom from = PlayFrom::Scene);
  void stopPlay();
  void togglePause();
  void stepFrame();
  HostedEngine* game() { return _game.get(); }
  // The Game view feeds frames and focus through these.
  unsigned advanceGame(int width, int height, float dt);
  void setGameFocused(bool focused);
  bool gameHasKeyboard() const { return _gameFocused && playing(); }
  // The running game, inspected live: the Hierarchy lists its entities while
  // `showLive`; one can be selected (edits last until Stop).
  bool& showLive() { return _showLive; }
  std::optional<EntityId> liveSelection() const { return _liveSelection; }
  void selectLive(std::optional<EntityId> id) { _liveSelection = id; }
  // Play was asked for and waits on a build.
  bool playPending() const { return _playAfterBuild; }

  // Builds.
  void build();
  CliRunner& cli() { return _cli; }
  bool buildStale() const { return _buildStale; }
  // Bumps after every successful build (views holding their own engine restart on it).
  uint64_t buildGeneration() const { return _buildGeneration; }
  // Runs `jm export` with these arguments ("--target", ...), then reveals the result.
  void exportGame(std::vector<std::string> args, std::string outDir);

  // Shared services.
  Commands& commands() { return _commands; }
  Toasts& toasts() { return _toasts; }
  Tool tool() const { return _tool; }
  void setTool(Tool tool) { _tool = tool; }
  void focusPanel(const char* name);  // "Scene", "Game", "Console", ...
  void revealAsset(const std::string& path);
  void openPalette(const std::string& prefix = {});
  void revealInFileManager(const std::filesystem::path& path);
  void openInCodeEditor(const std::string& path, int line = 0);

  ScenePanel& scenePanel() { return *_scenePanel; }
  // The folder the Assets panel shows (where OS drops land).
  std::string assetsFolder() const;
  // Where "Make Prefab" puts one: the Assets panel's folder when it holds
  // prefabs, else assets/prefabs.
  std::string assetsFolderForPrefabs() const;

 private:
  // whenSaved for the open document only (not the scene behind a prefab).
  void whenCurrentSaved(std::function<void()> then);
  void runAfterSave();
  // Back to the scene behind the open prefab, as it was left.
  void leavePrefab();

  std::optional<Project> _project;
  std::optional<SceneDocument> _scene;
  std::vector<EntityUid> _selection;
  std::string _inspectedAsset;
  // The scene behind an open prefab, kept as it was (unsaved edits, undo) until return.
  struct PrefabReturn {
    std::unique_ptr<SceneDocument> scene;
    std::vector<EntityUid> selection;
  };
  std::optional<PrefabReturn> _prefabReturn;
  std::set<EntityUid> _hidden;
  bool _clearConsoleOnPlay = true;
  Preview _preview;
  std::unique_ptr<HostedEngine> _game;
  bool _paused = false;
  bool _stepRequested = false;
  bool _gameFocused = false;
  bool _showLive = false;
  std::optional<EntityId> _liveSelection;
  bool _playAfterBuild = false;
  PlayFrom _playFrom = PlayFrom::Scene;
  CliRunner _cli;
  bool _buildStale = false;
  bool _lastBuildFailed = false;
  uint64_t _buildGeneration = 0;
  std::map<std::string, std::filesystem::file_time_type> _builtFiles;  // inputs of the last build
  double _lastScan = 0;
  double _changeSeen = 0;  // when a source change was first noticed (debounce)
  std::string _exportOut;
  std::optional<std::vector<std::string>> _exportAfterBuild;  // an export asked for during a build
  Commands _commands;
  Toasts _toasts;
  Tool _tool = Tool::Move;
  char _brushTile = '#';
  bool _quitConfirmed = false;
  std::function<void()> _afterSave;  // pending action behind the unsaved-changes prompt
  struct Prompt {
    std::string title, label, text;
    std::function<void(const std::string&)> done;
    bool opening = true;
  };
  std::optional<Prompt> _prompt;
  void drawPrompt();
  bool _askSave = false;
  std::string _focusRequest;
  bool _resetLayout = false;
  bool _showShortcuts = false;
  bool _showHistory = false;
  void drawHistory();
  struct AssetTab {
    std::unique_ptr<AssetDocument> doc;
    std::unique_ptr<AssetEditor> view;
    bool focus = true;  // bring it forward on the next frame
  };
  std::vector<AssetTab> _assetTabs;
  std::string _activeAsset;  // path of the tab used last, until the scene is used again
  void drawAssetTabs();
  // Saves asset documents whose edits have settled (or all of them, `now`).
  void saveAssets(bool now);

  std::unique_ptr<WelcomeScreen> _welcome;
  std::unique_ptr<HierarchyPanel> _hierarchy;
  std::unique_ptr<InspectorPanel> _inspector;
  std::unique_ptr<ScenePanel> _scenePanel;
  std::unique_ptr<GamePanel> _gamePanel;
  std::unique_ptr<AssetsPanel> _assets;
  std::unique_ptr<ConsolePanel> _console;
  std::unique_ptr<CommandPalette> _palette;
  std::unique_ptr<ExportDialog> _export;
  std::unique_ptr<SettingsDialog> _settings;

  void registerCommands();
  void drawWorkspace(float dt);
  void drawMenuBar();
  void drawToolbar();
  void drawStatusBar();
  void drawSavePrompt();
  void drawShortcuts();
  void setupDockLayout(unsigned dockspace);
  void watchFiles();
  // Rescans the project now and updates buildStale (no waiting for the watcher).
  void refreshBuildState();
  void onBuildFinished(const CliRunner::Finished& done);
  void restartPreview();
  void snapshotBuildInputs();
  // Writes a saved file into build/ too, so the preview and play see it without a rebuild.
  void writeThrough(const std::string& path);
  void loadSchemas();
  // Unsaved work is written to a recovery file now and then; a crash loses little.
  void autosave();
  void offerRecovery();
  std::filesystem::path recoveryFile() const;
  double _lastAutosave = 0;
  void playSceneFile();
};
