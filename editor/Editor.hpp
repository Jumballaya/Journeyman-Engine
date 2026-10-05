#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

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

  // Project.
  bool openProject(const std::filesystem::path& folder);
  void closeProject();
  Project* project() { return _project ? &*_project : nullptr; }

  // Scene documents.
  SceneDocument* scene() { return _scene ? &*_scene : nullptr; }
  void openScene(const std::string& path);
  void newScene();
  bool saveScene();
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

  // Entity actions on the selection or at a world point.
  void duplicateSelection();
  void deleteSelection();
  // The selection as JSON on the system clipboard; paste adds it to the open scene.
  void copySelection();
  void paste();
  EntityUid createEntity(const std::string& kind, glm::vec2 at);
  // Drops an asset into the scene: a prefab instance, a sprite, a tile map, a UI screen...
  EntityUid instantiateAsset(const std::string& path, glm::vec2 at);
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
  void startPlay();
  void stopPlay();
  void togglePause();
  void stepFrame();
  HostedEngine* game() { return _game.get(); }
  // The Game view feeds frames and focus through these.
  unsigned advanceGame(int width, int height, float dt);
  void setGameFocused(bool focused);
  bool gameHasKeyboard() const { return _gameFocused && playing(); }

  // Builds.
  void build();
  CliRunner& cli() { return _cli; }
  bool buildStale() const { return _buildStale; }
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

 private:
  std::optional<Project> _project;
  std::optional<SceneDocument> _scene;
  std::vector<EntityUid> _selection;
  Preview _preview;
  std::unique_ptr<HostedEngine> _game;
  bool _paused = false;
  bool _stepRequested = false;
  bool _gameFocused = false;
  bool _playAfterBuild = false;
  CliRunner _cli;
  bool _buildStale = false;
  std::map<std::string, std::filesystem::file_time_type> _builtFiles;  // inputs of the last build
  double _lastScan = 0;
  double _changeSeen = 0;  // when a source change was first noticed (debounce)
  std::string _exportOut;
  Commands _commands;
  Toasts _toasts;
  Tool _tool = Tool::Move;
  char _brushTile = '#';
  bool _quitConfirmed = false;
  std::function<void()> _afterSave;  // pending action behind the unsaved-changes prompt
  bool _askSave = false;
  std::string _focusRequest;
  bool _resetLayout = false;
  bool _showShortcuts = false;

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
  std::string playSceneFile();
};
