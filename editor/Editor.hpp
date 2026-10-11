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
#include "EditorSession.hpp"
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
class SessionDialog;
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
  // Tells jm what's open and unsaved here (EditorSession); call each frame, minimized too.
  void publishSession();
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
  // Names a new scene and creates it (in `folder` when that is under scenes/), then opens it.
  void newScene(const std::string& folder = {});
  bool saveScene();
  void saveSceneAs();
  // Asks for a name in a small dialog; `done` gets it (trimmed, non-empty).
  // `where`, when given, reads back what the typed name will make (a path), under the field.
  void prompt(std::string title, std::string label, std::string initial, std::function<void(const std::string&)> done,
              std::function<std::string(const std::string&)> where = {});
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
  // `item` (if given) is selected in it (see AssetEditor::show).
  void openAsset(const std::string& path, const std::string& item = {});
  // Plays a project sound through the preview engine, stopping whatever played before; "" just stops.
  void previewSound(const std::string& path);
  // Opens `scene` with the entity `pick` accepts (by its effective components) selected and framed.
  void openSceneAt(const std::string& scene, const std::function<bool(const Json& components)>& pick);
  // Scenes with an entity drawing the map file `path`, for "paint it there".
  std::vector<std::string> scenesUsingMap(const std::string& path);
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
  // "tileset", "atlas", "data", "input", "ui") from a template, after asking
  // for its name; then `created` gets its path, and it opens in its editor.
  // With no `folder`, it goes where files of its kind already are.
  void newAsset(const std::string& kind, const std::string& folder = {},
                std::function<void(const std::string& path)> created = {});
  // What newAsset can make that a field taking `types` (".tileset.json"...) would accept.
  struct NewAssetKind {
    const char* kind;   // for newAsset
    const char* title;  // "New Tileset"
  };
  static std::vector<NewAssetKind> newAssetKindsFor(const std::vector<std::string>& types);
  // Edits an asset open in a tab (an undoable step there); nothing if it isn't open.
  void editOpenAsset(const std::string& path, const std::string& label, const std::function<void(Json&)>& change);
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
  // What Create offers (menus, palette, Create Here): a kind for createEntity, and its icon.
  struct CreateKind {
    const char* kind;
    const char* icon;
  };
  static const std::vector<CreateKind>& createKinds();
  // A new entity of `kind` at `at`, nudged off any entity already standing there; selected.
  EntityUid createEntity(const std::string& kind, glm::vec2 at);
  // Adds a component (newComponent) to every selected entity that lacks it.
  void addComponent(const std::string& component);
  // The selection minus entities inside other selected ones (they move with those).
  std::vector<EntityUid> selectionRoots() const;
  // Nesting. Moves entities inside `parent` (0: the scene's top level; in a
  // prefab, its root) at index `at` among its children (-1: last), each
  // keeping where it stands in the world.
  void reparent(const std::vector<EntityUid>& uids, EntityUid parent, int at = -1);
  // A new empty entity inside `parent`, selected.
  EntityUid createChild(EntityUid parent);
  // Where the preview placed an entity in the world (children from their parents).
  std::optional<TransformComponent> worldTransform(EntityUid uid) const;
  // Drops an asset into the scene: a prefab instance, a sprite, a tile map, a UI screen...
  EntityUid instantiateAsset(const std::string& path, glm::vec2 at);
  // Hidden in the Scene view only (an editing aid; the game and saves are unaffected).
  bool hiddenInView(EntityUid uid) const { return _hidden.contains(uid); }
  void toggleHiddenInView(EntityUid uid) { _hidden.contains(uid) ? (void)_hidden.erase(uid) : (void)_hidden.insert(uid); }
  bool& clearConsoleOnPlay() { return _clearConsoleOnPlay; }
  // Drops an asset onto an entity: a script, image, sound, UI document or
  // tileset sets (or adds) the matching component. False if it doesn't apply.
  bool applyAssetToEntity(EntityUid uid, const std::string& path, bool dryRun = false);
  // A scene entry as the engine should spawn it: prefab merged, maps being
  // painted inlined, editor ids kept.
  Json resolveForEngine(const Json& entity);

  // Tile maps (Tiled .tmj files) being painted: edited through the scene
  // document so painting undoes with everything else, and saved with it.
  // The map file an entity draws ("" for none).
  std::string mapPathOf(EntityUid uid);
  // A map as edited (null if unreadable); edits are undoable scene steps.
  const Json* map(const std::string& path);
  void editMap(const std::string& path, const std::string& label, const std::function<void(Json& map)>& mutate,
               const std::string& mergeKey = {});
  // A tileset (.tsj) as edited: its open tab's, else the file's; null if unreadable.
  const Json* tileset(const std::string& path);
  // Opens a Tiled file in Tiled (when it's installed).
  void openInTiled(const std::string& path);
  // What the tile tools paint: a tile (with Tiled flip flags), or a terrain of
  // its tileset (a wang set and color) when `terrainSet` >= 0.
  struct TileBrush {
    std::string tileset;
    uint32_t tile = 0, flips = 0;
    int terrainSet = -1, terrainColor = 1;
  };
  TileBrush& tileBrush() { return _tileBrush; }
  // The layer the tile tools work on, per map (an index into its layers).
  int& activeLayer(const std::string& map) { return _activeLayers[map]; }
  // The map object selected for editing (its id), or 0.
  int& selectedObject() { return _selectedObject; }

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

  // Multiplayer: the whole session on this machine, in windows of their own
  // (`jm run --peers`), built first if needed. Its output goes to the Console.
  void playSession(int players, int latencyMs, float loss);
  void stopSession() { _session.cancel(); }
  bool sessionRunning() const { return _session.busy(); }

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
  // The file a typed name makes in `folder`: spaces and slashes to underscores,
  // `extension` once, numbered past any file that exists.
  std::string freePath(const std::string& folder, std::string typed, const std::string& extension) const;
  // Where files of a kind live: the folder holding the most of them, else `fallback`.
  std::string kindFolder(AssetKind kind, const std::string& fallback) const;
  // whenSaved for the open document only (not the scene behind a prefab).
  void whenCurrentSaved(std::function<void()> then);
  void runAfterSave();
  // Back to the scene behind the open prefab, as it was left.
  void leavePrefab();
  // Makes `doc` the open document, with nothing selected or hidden.
  void setScene(std::optional<SceneDocument> doc);
  // An error toast whose action shows the Console.
  void consoleError(const std::string& title, const std::string& body);
  // Writes a prefab file; on failure toasts `failure` and returns false.
  bool writePrefab(const std::string& path, Json prefab, const std::string& failure);

  std::optional<Project> _project;
  std::optional<SceneDocument> _scene;
  std::vector<EntityUid> _selection;
  std::string _inspectedAsset;
  // The scene behind an open prefab, kept as it was (unsaved edits, undo) until return.
  struct PrefabReturn {
    std::unique_ptr<SceneDocument> scene;
    std::vector<EntityUid> selection;
    std::set<EntityUid> hidden;
  };
  std::optional<PrefabReturn> _prefabReturn;
  std::set<EntityUid> _hidden;
  bool _clearConsoleOnPlay = true;
  Preview _preview;
  std::unique_ptr<HostedEngine> _game;
  std::string _gameNotice;  // the game's last notice (a marker saved), shown once
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
  bool _buildCancelled = false;  // the last build was stopped on purpose
  uint64_t _buildGeneration = 0;
  std::map<std::string, std::filesystem::file_time_type> _builtFiles;  // inputs of the last build
  double _lastScan = 0;
  double _changeSeen = 0;  // when a source change was first noticed (debounce)
  std::string _exportOut;
  std::function<void()> _queuedExport;  // an export asked for while the CLI was busy
  CliRunner _session;                   // jm run --peers: runs beside builds
  std::function<void()> _queuedSession; // a session waiting on a build
  Commands _commands;
  Toasts _toasts;
  Tool _tool = Tool::Move;
  TileBrush _tileBrush;
  std::map<std::string, int> _activeLayers;
  int _selectedObject = 0;
  // Parsed project files by path, kept while unchanged on disk.
  std::map<std::string, std::pair<std::filesystem::file_time_type, Json>> _parsed;
  const Json* parsedFile(const std::string& path);
  // Where new entities go: a prefab's root, else the scene's top level (0).
  EntityUid newEntityParent() const;
  // `at`, or the nearest spot down-right of it with no entity standing on it.
  glm::vec2 freeSpot(glm::vec2 at) const;
  // A world point as a new entity's position there, [x, y, 0] (whole pixels).
  Json localPosition(glm::vec2 world) const;
  // A new map's file: 20 x 15 tiles drawing from the project's first tileset.
  std::string newMapText(const std::string& path);
  // Copies a project file into build/ when it's missing there or older, for the preview.
  void mirrorToBuild(const std::string& path);
  // Changes a map file on disk (one not being painted).
  void editMapFile(const std::string& path, const std::function<void(Json&)>& mutate);
  bool _quitConfirmed = false;
  std::function<void()> _afterSave;  // pending action behind the unsaved-changes prompt
  struct Prompt {
    std::string title, label, text;
    std::function<void(const std::string&)> done;
    std::function<std::string(const std::string&)> where;
    bool opening = true;
  };
  std::optional<Prompt> _prompt;
  double _previewRetry = 0;  // when a preview that failed to start last tried again
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
  std::unique_ptr<SessionDialog> _sessionDialog;

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
  std::optional<EditorSession> _editorSession;  // while a project is open
  void offerRecovery();
  std::filesystem::path recoveryFile() const;
  double _lastAutosave = 0;
  struct {
    double time = -10;
    int files = 0;
    std::string title;  // its toast's
  } _lastImport;
  void playSceneFile();
};
