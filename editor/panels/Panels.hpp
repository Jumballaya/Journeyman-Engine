#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <imgui.h>

#include "Editor.hpp"

// Each panel draws one dockable window from the Editor's state and acts
// through the Editor; what it keeps itself is only view state.

class WelcomeScreen {
 public:
  void draw(Editor& editor);

 private:
  struct Example {
    std::filesystem::path folder;
    std::string name;
  };
  std::string _error;
  std::string _filter;
  std::filesystem::path _creating;  // a new project waiting on `jm init`
  std::vector<RecentProject> _recents;
  std::vector<Example> _examples;
  double _loadedAt = -100;
};

class HierarchyPanel {
 public:
  void draw(Editor& editor);
  void rename(EntityUid uid) { _renaming = uid, _renameFocus = true; }

 private:
  void drawLive(Editor& editor);
  void drawGroupHeader(Editor& editor, const std::string& group, std::optional<std::pair<EntityUid, std::string>>& regroup);
  std::string _filter;
  EntityUid _renaming = 0;
  bool _renameFocus = false;
  std::string _renameText;
  EntityUid _lastClicked = 0;  // anchor for Shift-click ranges
};

class InspectorPanel {
 public:
  void draw(Editor& editor);

 private:
  double _soundStarted = 0;  // a previewed sound is playing since then
  std::string _newTag;
  void drawAsset(Editor& editor, const std::string& reference);
  void prefabBar(Editor& editor, EntityUid uid, const Json& entity);
  void drawLive(Editor& editor, EntityId id);
  void drawSceneOverview(Editor& editor, SceneDocument& scene);

  std::string _addFilter;
  std::map<std::string, std::string> _jsonDrafts;  // Json fields being typed, by id
};

class ScenePanel {
 public:
  void draw(Editor& editor, float dt);
  // Moves the view onto the selection / everything, easing there.
  void frameSelection(Editor& editor);
  void frameAll(Editor& editor);
  void setZoom(float zoom);
  float zoom() const { return _zoom; }
  glm::vec2 viewCenter() const { return _center; }
  // The world point under the mouse (or the view's center if outside).
  glm::vec2 cursorWorld() const { return _cursorWorld; }
  bool hovered() const { return _hovered; }
  bool animating() const { return _targetCenter || _targetZoom || _drag != Drag::None; }
  bool& showGrid() { return _showGrid; }
  bool& snap() { return _snap; }
  bool& showColliders() { return _showColliders; }
  bool& showGameFrame() { return _showGameFrame; }
  bool& showUi() { return _showUi; }
  float& gridSize() { return _gridSize; }

 private:
  glm::vec2 _center{0.0f};
  float _zoom = 1.0f;
  std::optional<glm::vec2> _targetCenter;
  std::optional<float> _targetZoom;
  ImVec2 _origin{}, _size{};  // the view's screen rectangle
  ImVec2 _lastSize{};
  glm::vec2 _cursorWorld{0.0f};
  bool _hovered = false;
  bool _showGrid = true, _snap = false, _showColliders = true, _showGameFrame = true, _showUi = true;
  float _gridSize = 16.0f;
  std::string _framedScene;
  std::array<float, 3> _savedCamera{};
  float _cameraRestSince = 0;

  // An in-progress drag: moving, rotating, scaling, box-selecting or panning.
  enum class Drag { None, Move, MoveX, MoveY, Rotate, Scale, Box, Pan, Paint, Object };
  Drag _drag = Drag::None;
  glm::vec2 _dragStart{0.0f};
  std::map<EntityUid, Json> _dragOriginals;  // transforms at drag start (a child's: relative to its parent)
  struct DragFrame {
    glm::vec2 world{0.0f};    // where it was in the world
    float parentTurn = 0.0f;  // its parent's rotation: world moves turn by -this into its own frame
  };
  std::map<EntityUid, DragFrame> _dragFrames;
  std::optional<glm::ivec2> _lastPaintCell;
  int _movingObject = 0;       // the map object being dragged (0: one being drawn)
  glm::vec4 _movingFrom{0.0f};  // its rectangle when the drag began
  float _paletteHeight = 120.0f;

  glm::vec2 toWorld(ImVec2 screen) const;
  ImVec2 toScreen(glm::vec2 world) const;
  void handleInput(Editor& editor);
  void handleTilePainting(Editor& editor);
  // Object layers: draws the map's objects; drag to draw, move; Delete removes.
  void handleMapObjects(Editor& editor, const std::string& path, glm::vec2 origin);
  void drawGrid(ImDrawList* draw);
  void drawGameFrame(Editor& editor, ImDrawList* draw);
  void drawSelection(Editor& editor, ImDrawList* draw);
  void drawGizmo(Editor& editor, ImDrawList* draw);
  void drawOverlayToolbar(Editor& editor);
  void drawTilePalette(Editor& editor);
  void drawDropTarget(Editor& editor);
  // Opens the UI screen element under a world point in the UI editor; false if none is there.
  bool openUiAt(Editor& editor, glm::vec2 world);
  void applyTransformDrag(Editor& editor, glm::vec2 world, bool fine);
  glm::vec2 snapped(glm::vec2 p, bool force) const;
};

class GamePanel {
 public:
  void draw(Editor& editor, float dt);

 private:
  // The selected running entity's corners on screen, if it has a transform.
  std::optional<std::array<ImVec2, 4>> liveOutline(Editor& editor, ImVec2 at, ImVec2 size);
  void forwardMouse(Editor& editor, ImVec2 at, bool overGame);
  int _scaleMode = 0;  // 0 fit, 1 pixel perfect
  bool _buttonsDown[3] = {};  // pressed over the game, not yet released
  ImVec2 _pointer{-1.0f, -1.0f};  // where the game was last told the pointer is
};

class AssetsPanel {
 public:
  void draw(Editor& editor);
  void reveal(const std::string& path);
  const std::string& folder() const { return _folder; }

 private:
  std::string _folder = "assets";
  std::string _filter;
  std::string _selected;
  float _tileSize = 84.0f;
  bool _listView = false;
  std::string _renaming;
  std::string _renameText;

  void drawFolderTree(Editor& editor, const std::string& folder, int depth);
  void drawGrid(Editor& editor);
  void open(Editor& editor, const std::string& path);
  void contextMenu(Editor& editor, const std::string& path, bool isFolder);
};

class ConsolePanel {
 public:
  void draw(Editor& editor);

 private:
  bool _showLevel[3] = {true, true, true};  // by LogBook::Level
  int _source = 0;                          // 0 all, 1 game, 2 build
  std::string _filter;
  uint64_t _seenVersion = 0;
  bool _stickToBottom = true;
};

class CommandPalette {
 public:
  void open(const std::string& prefix = {});
  void draw(Editor& editor);

 private:
  bool _open = false, _justOpened = false;
  std::string _query;
  int _cursor = 0;
};

class ExportDialog {
 public:
  void open() { _open = true; }
  void draw(Editor& editor);

 private:
  bool _open = false;
  int _target = 0;
  bool _bare = false;
  bool _server = false;  // the dedicated server instead of the game
  std::string _out = "dist";
  std::string _player;
};

// "Play with Players": the whole multiplayer session on this machine (jm run
// --peers): the game's server, if it has one, and a window per player.
class SessionDialog {
 public:
  void open() { _open = true; }
  void draw(Editor& editor);

 private:
  bool _open = false;
  int _players = 2;
  int _latency = 0;  // ms
  int _loss = 0;     // percent
};

class SettingsDialog {
 public:
  void open() { _open = true; }
  void draw(Editor& editor);

 private:
  bool _open = false;
  Json _draft;
  std::filesystem::path _draftRoot;  // the project the draft was taken from
  int _section = 0;
  std::string _newEntry;

  // Scenes in order, asset entries with what they match, and files left out.
  void contentSection(const Project& project);
  void multiplayerSection(const Project& project);
  // A field choosing one project file of `kind` (or none, shown as `none`).
  void fileChoice(const Project& project, Json& object, const char* key, const char* label, const char* hint, AssetKind kind,
                  const char* none);
};
