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
  bool& showGrid() { return _showGrid; }
  bool& snap() { return _snap; }
  bool& showColliders() { return _showColliders; }
  bool& showGameFrame() { return _showGameFrame; }
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
  bool _showGrid = true, _snap = false, _showColliders = true, _showGameFrame = true;
  float _gridSize = 16.0f;
  std::string _framedScene;
  std::array<float, 3> _savedCamera{};
  float _cameraRestSince = 0;

  // An in-progress drag: moving, rotating, scaling, box-selecting or panning.
  enum class Drag { None, Move, MoveX, MoveY, Rotate, Scale, Box, Pan, Paint };
  Drag _drag = Drag::None;
  glm::vec2 _dragStart{0.0f}, _dragLast{0.0f};
  std::map<EntityUid, Json> _dragOriginals;  // transforms at drag start
  std::vector<std::string> _paintRows;        // the map before a stroke
  std::optional<glm::ivec2> _lastPaintCell;
  bool _altCycle = false;

  glm::vec2 toWorld(ImVec2 screen) const;
  ImVec2 toScreen(glm::vec2 world) const;
  void handleInput(Editor& editor);
  void handleTilePainting(Editor& editor);
  void drawGrid(ImDrawList* draw);
  void drawGameFrame(Editor& editor, ImDrawList* draw);
  void drawSelection(Editor& editor, ImDrawList* draw);
  void drawGizmo(Editor& editor, ImDrawList* draw);
  void drawOverlayToolbar(Editor& editor);
  void drawTilePalette(Editor& editor);
  void drawDropTarget(Editor& editor);
  void applyTransformDrag(Editor& editor, glm::vec2 world, bool fine);
  glm::vec2 snapped(glm::vec2 p, bool force) const;
};

class GamePanel {
 public:
  void draw(Editor& editor, float dt);

 private:
  int _scaleMode = 0;  // 0 fit, 1 pixel perfect
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
  bool _showInfo = true, _showWarnings = true, _showErrors = true;
  int _source = 0;  // 0 all, 1 engine, 2 build
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
  std::string _out = "dist";
  std::string _player;
};

class SettingsDialog {
 public:
  void open() { _open = true, _loaded = false; }
  void draw(Editor& editor);

 private:
  bool _open = false, _loaded = false;
  Json _draft;
  int _section = 0;
};
