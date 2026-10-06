// The workspace around the panels: commands, menu bar, toolbar, dock layout,
// status bar and the unsaved-changes prompt.

#include <GLFW/glfw3.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <nfd.hpp>

#include "Editor.hpp"
#include "Entities.hpp"
#include "Icons.hpp"
#include "LogBook.hpp"
#include "Theme.hpp"
#include "Ui.hpp"
#include "panels/Panels.hpp"

namespace {

constexpr float kToolbarHeight = 44.0f;
constexpr float kStatusHeight = 26.0f;

bool hasTileMapSelected(Editor& e) {
  return e.scene() && e.primary() && e.preview().tileGrid(e.primary()) != nullptr;
}

const char* toolCommand(Tool tool) {
  switch (tool) {
    case Tool::Select: return "tool.select";
    case Tool::Move: return "tool.move";
    case Tool::Rotate: return "tool.rotate";
    case Tool::Scale: return "tool.scale";
    case Tool::Pan: return "tool.pan";
    case Tool::TileBrush: return "tool.tileBrush";
    case Tool::TileRect: return "tool.tileRect";
    case Tool::TileFill: return "tool.tileFill";
    case Tool::TileErase: return "tool.tileErase";
    case Tool::TilePick: return "tool.tilePick";
  }
  return "";
}

}  // namespace

void Editor::registerCommands() {
  auto hasProject = [this]() { return _project.has_value(); };
  auto hasScene = [this]() { return _scene.has_value(); };
  auto hasSelection = [this]() { return _scene && !_selection.empty(); };

  // File
  _commands.add({"project.open", "Open Project...", "File", ICON_FOLDER_OPEN, ImGuiMod_Ctrl | ImGuiKey_O, [this]() {
                   NFD::UniquePath folder;
                   if (NFD::PickFolder(folder) == NFD_OKAY) whenSaved([this, path = std::string(folder.get())]() { openProject(path); });
                 }});
  _commands.add({"project.close", "Close Project", "File", ICON_X_SQUARE, 0,
                 [this]() { whenSaved([this]() { closeProject(); }); }, hasProject});
  _commands.add({"scene.new", "New Scene", "File", ICON_FILE_PLUS, ImGuiMod_Ctrl | ImGuiKey_N, [this]() { newScene(); }, hasProject});
  _commands.add({"scene.open", "Open Scene...", "File", ICON_FILM_SLATE, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_O,
                 [this]() { openPalette("scene "); }, hasProject});
  // Asset tabs save on their own; Save just does it now (and the scene, if one is open).
  _commands.add({"scene.save", "Save", "File", ICON_FLOPPY_DISK, ImGuiMod_Ctrl | ImGuiKey_S, [this]() {
                   saveAssets(true);
                   if (_scene) saveScene();
                 }, [this, hasScene]() { return hasScene() || !_assetTabs.empty(); }});
  _commands.add({"prefab.make", "Make Prefab from Selection", "Prefab", ICON_CUBE, 0, [this]() {
                   for (EntityUid uid : std::vector<EntityUid>(_selection)) createPrefab(uid, assetsFolderForPrefabs());
                 }, [this]() { return _scene && !_scene->isPrefab() && !_selection.empty(); }});
  _commands.add({"prefab.back", "Back to Scene", "Prefab", ICON_ARROW_LEFT, 0, [this]() { returnFromPrefab(); },
                 [this]() { return !returnScene().empty() && _scene && _scene->isPrefab(); }});
  _commands.add({"prefab.apply", "Apply Overrides to Prefab", "Prefab", ICON_UPLOAD_SIMPLE, 0, [this]() { applyOverrides(primary()); },
                 [this]() { const Json* e = _scene ? _scene->find(primary()) : nullptr; return e && e->contains("prefab"); }});
  _commands.add({"scene.saveAs", "Save As...", "File", ICON_FLOPPY_DISK_BACK, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S,
                 [this]() { saveSceneAs(); }, hasScene});
  _commands.add({"project.build", "Build", "File", ICON_HAMMER, ImGuiMod_Ctrl | ImGuiKey_B, [this]() { build(); },
                 [this]() { return _project && !_cli.busy(); }});
  _commands.add({"project.export", "Export Game...", "File", ICON_PACKAGE, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_E,
                 [this]() { _export->open(); }, hasProject});
  _commands.add({"project.settings", "Project Settings...", "File", ICON_GEAR_SIX, ImGuiMod_Ctrl | ImGuiKey_Comma,
                 [this]() { _settings->open(); }, hasProject});
  _commands.add({"project.reveal", "Reveal Project in File Manager", "File", ICON_FOLDER_SIMPLE, 0,
                 [this]() { revealInFileManager(_project->abs(".jm.json")); }, hasProject});
  _commands.add({"app.quit", "Quit", "File", ICON_SIGN_OUT, ImGuiMod_Ctrl | ImGuiKey_Q, [this]() { requestQuit(); }});

  // Edit
  _commands.add({"edit.undo", "Undo", "Edit", ICON_ARROW_COUNTER_CLOCKWISE, ImGuiMod_Ctrl | ImGuiKey_Z,
                 [this]() { activeAsset() ? activeAsset()->undo() : _scene->undo(); },
                 [this]() { return activeAsset() ? activeAsset()->canUndo() : _scene && _scene->canUndo(); }});
  _commands.add({"edit.redo", "Redo", "Edit", ICON_ARROW_CLOCKWISE, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z,
                 [this]() { activeAsset() ? activeAsset()->redo() : _scene->redo(); },
                 [this]() { return activeAsset() ? activeAsset()->canRedo() : _scene && _scene->canRedo(); }});
  _commands.add({"edit.copy", "Copy", "Edit", ICON_COPY_SIMPLE, ImGuiMod_Ctrl | ImGuiKey_C, [this]() { copySelection(); }, hasSelection});
  _commands.add({"edit.cut", "Cut", "Edit", ICON_SCISSORS, ImGuiMod_Ctrl | ImGuiKey_X, [this]() {
                   copySelection();
                   deleteSelection();
                 }, hasSelection});
  _commands.add({"edit.paste", "Paste", "Edit", ICON_CLIPBOARD, ImGuiMod_Ctrl | ImGuiKey_V, [this]() { paste(); },
                 [this]() { return _scene && !_scene->isPrefab(); }});
  // An asset tab in use takes these for what's selected in it.
  auto inTabOr = [this](const char* id, std::function<void()> fallback) {
    return [this, id, fallback]() {
      if (!assetCommand(id, true)) fallback();
    };
  };
  auto tabOr = [this, hasSelection](const char* id) {
    return [this, id, hasSelection]() { return assetCommand(id, false) || hasSelection(); };
  };
  _commands.add({"edit.duplicate", "Duplicate", "Edit", ICON_COPY, ImGuiMod_Ctrl | ImGuiKey_D,
                 inTabOr("edit.duplicate", [this]() { duplicateSelection(); }), tabOr("edit.duplicate")});
  _commands.add({"edit.delete", "Delete", "Edit", ICON_TRASH, ImGuiKey_Delete, inTabOr("edit.delete", [this]() { deleteSelection(); }),
                 tabOr("edit.delete")});
  _commands.add({"edit.deleteBack", "Delete", "Edit", ICON_TRASH, ImGuiKey_Backspace,
                 inTabOr("edit.delete", [this]() { deleteSelection(); }), tabOr("edit.delete")});
  _commands.add({"edit.rename", "Rename", "Edit", ICON_PENCIL_SIMPLE, ImGuiKey_F2, [this]() {
                   _hierarchy->rename(primary());
                   focusPanel("Hierarchy");
                 }, [this]() { return _scene && _selection.size() == 1 && !_scene->isPrefab(); }});
  _commands.add({"edit.selectAll", "Select All", "Edit", ICON_SELECTION_ALL, ImGuiMod_Ctrl | ImGuiKey_A, [this]() {
                   std::vector<EntityUid> all;
                   for (size_t i = 0; i < _scene->size(); ++i) all.push_back(_scene->uid(i));
                   selectAll(all);
                 }, hasScene});
  _commands.add({"edit.deselect", "Deselect", "Edit", ICON_SELECTION_SLASH, ImGuiKey_Escape, [this]() { _selection.clear(); },
                 hasSelection});

  // Create
  for (const auto& [kind, icon] : std::vector<std::pair<std::string, const char*>>{
           {"Empty", ICON_CUBE_TRANSPARENT}, {"Sprite", ICON_IMAGE}, {"Text", ICON_TEXT_T}, {"Tile Map", ICON_GRID_FOUR},
           {"UI Screen", ICON_BROWSER}, {"Sound", ICON_SPEAKER_HIGH}, {"Script", ICON_CODE}}) {
    _commands.add({"create." + kind, "Create " + kind, "Create", icon, 0,
                   [this, kind]() { createEntity(kind, _scenePanel->viewCenter()); },
                   [this]() { return _scene && !_scene->isPrefab(); }});
  }

  // View and tools
  _commands.add({"view.frame", "Frame Selection", "View", ICON_CORNERS_OUT, ImGuiKey_F, [this]() { _scenePanel->frameSelection(*this); },
                 hasScene});
  _commands.add({"view.frameAll", "Frame Scene", "View", ICON_FRAME_CORNERS, ImGuiKey_Home, [this]() { _scenePanel->frameAll(*this); },
                 hasScene});
  _commands.add({"view.actualSize", "Zoom to 100%", "View", ICON_MAGNIFYING_GLASS, ImGuiMod_Ctrl | ImGuiKey_0,
                 [this]() { _scenePanel->setZoom(1.0f); }, hasScene});
  _commands.add({"view.grid", "Toggle Grid", "View", ICON_GRID_NINE, ImGuiMod_Ctrl | ImGuiKey_Apostrophe,
                 [this]() { _scenePanel->showGrid() = !_scenePanel->showGrid(); }, hasScene});
  _commands.add({"view.snap", "Toggle Snapping", "View", ICON_MAGNET, ImGuiMod_Shift | ImGuiKey_G,
                 [this]() { _scenePanel->snap() = !_scenePanel->snap(); }, hasScene});
  _commands.add({"view.colliders", "Toggle Collider Outlines", "View", ICON_BOUNDING_BOX, 0,
                 [this]() { _scenePanel->showColliders() = !_scenePanel->showColliders(); }, hasScene});
  _commands.add({"view.ui", "Toggle Game UI in Scene", "View", ICON_BROWSER, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_U,
                 [this]() { _scenePanel->showUi() = !_scenePanel->showUi(); }, hasScene});
  _commands.add({"view.palette", "Command Palette...", "View", ICON_COMMAND, ImGuiMod_Ctrl | ImGuiKey_K, [this]() { openPalette(); }});
  _commands.add({"view.palette2", "Command Palette...", "View", ICON_COMMAND, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P,
                 [this]() { openPalette(); }});
  _commands.add({"view.layout", "Reset Layout", "View", ICON_LAYOUT, 0, [this]() { _resetLayout = true; }, hasProject});
  _commands.add({"view.history", "Undo History", "View", ICON_CLOCK_COUNTER_CLOCKWISE, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_H,
                 [this]() { _showHistory = !_showHistory; }, hasScene});
  for (const auto& [panel, key] : std::vector<std::pair<const char*, ImGuiKey>>{
           {"Scene", ImGuiKey_1}, {"Game", ImGuiKey_2}, {"Hierarchy", ImGuiKey_3}, {"Inspector", ImGuiKey_4},
           {"Assets", ImGuiKey_5}, {"Console", ImGuiKey_6}}) {
    _commands.add({std::string("view.panel.") + panel, std::string("Show ") + panel, "View", ICON_APP_WINDOW,
                   ImGuiMod_Ctrl | key, [this, panel]() { focusPanel(panel); }, hasProject});
  }

  struct ToolInfo {
    Tool tool;
    const char* id;
    const char* label;
    const char* icon;
    ImGuiKey key;
    bool tile;
  };
  for (const ToolInfo& t : std::vector<ToolInfo>{
           {Tool::Select, "tool.select", "Select Tool", ICON_CURSOR, ImGuiKey_Q, false},
           {Tool::Move, "tool.move", "Move Tool", ICON_ARROWS_OUT_CARDINAL, ImGuiKey_W, false},
           {Tool::Rotate, "tool.rotate", "Rotate Tool", ICON_ARROWS_CLOCKWISE, ImGuiKey_E, false},
           {Tool::Scale, "tool.scale", "Scale Tool", ICON_ARROWS_OUT, ImGuiKey_R, false},
           {Tool::Pan, "tool.pan", "Hand Tool", ICON_HAND, ImGuiKey_H, false},
           {Tool::TileBrush, "tool.tileBrush", "Tile Brush", ICON_PAINT_BRUSH_BROAD, ImGuiKey_B, true},
           {Tool::TileRect, "tool.tileRect", "Tile Rectangle", ICON_RECTANGLE, ImGuiKey_U, true},
           {Tool::TileFill, "tool.tileFill", "Tile Fill", ICON_PAINT_BUCKET, ImGuiKey_G, true},
           {Tool::TileErase, "tool.tileErase", "Tile Eraser", ICON_ERASER, ImGuiKey_X, true},
           {Tool::TilePick, "tool.tilePick", "Tile Picker", ICON_EYEDROPPER, ImGuiKey_I, true}}) {
    _commands.add({t.id, t.label, "Tools", t.icon, t.key, [this, tool = t.tool]() { _tool = tool; },
                   t.tile ? std::function<bool()>([this]() { return hasTileMapSelected(*this); }) : hasScene});
  }

  // Play
  _commands.add({"play.toggle", "Play / Stop", "Play", ICON_PLAY, ImGuiMod_Ctrl | ImGuiKey_P,
                 [this]() { playing() ? stopPlay() : startPlay(); }, hasScene, true});
  _commands.add({"play.game", "Play Game (from the first scene)", "Play", ICON_GAME_CONTROLLER, ImGuiKey_F5,
                 [this]() { playing() ? stopPlay() : startPlay(PlayFrom::Game); }, hasProject, true});
  _commands.add({"play.scene", "Play This Scene", "Play", ICON_PLAY, ImGuiKey_F6,
                 [this]() { playing() ? stopPlay() : startPlay(PlayFrom::Scene); }, hasScene, true});
  _commands.add({"play.pause", "Pause / Resume", "Play", ICON_PAUSE, ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_P,
                 [this]() { togglePause(); }, [this]() { return playing(); }, true});
  _commands.add({"play.step", "Step One Frame", "Play", ICON_SKIP_FORWARD, ImGuiKey_F10, [this]() { stepFrame(); },
                 [this]() { return playing(); }, true});

  // Help
  _commands.add({"help.shortcuts", "Keyboard Shortcuts", "Help", ICON_KEYBOARD, ImGuiMod_Ctrl | ImGuiKey_Slash,
                 [this]() { _showShortcuts = true; }});
}

void Editor::drawWorkspace(float dt) {
  drawMenuBar();
  drawToolbar();

  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos({vp->WorkPos.x, vp->WorkPos.y + kToolbarHeight});
  ImGui::SetNextWindowSize({vp->WorkSize.x, vp->WorkSize.y - kToolbarHeight - kStatusHeight});
  ImGui::SetNextWindowViewport(vp->ID);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {4, 0});
  ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::bg0);
  ImGui::Begin("##dockhost", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings);
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
  const ImGuiID dockspace = ImGui::GetID("workspace");
  if (_resetLayout || !ImGui::DockBuilderGetNode(dockspace)) {
    setupDockLayout(dockspace);
    _resetLayout = false;
  }
  // Splitters in the ground color read as gutters between panels.
  ImGui::PushStyleColor(ImGuiCol_Separator, theme::bg0);
  ImGui::DockSpace(dockspace, {0, 0}, ImGuiDockNodeFlags_None);
  ImGui::PopStyleColor();
  ImGui::End();

  // Panels sync the preview before drawing it.
  if (_scene) {
    _preview.sync(*_scene, [this](const Json& e) { return resolveForEngine(e); },
                  [this](EntityUid uid) { return !hiddenInView(uid); });
    std::erase_if(_selection, [this](EntityUid uid) { return _scene->indexOf(uid) < 0; });
  }
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
  if (ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
    ImGui::PopStyleVar();
    _scenePanel->draw(*this, dt);
  } else {
    ImGui::PopStyleVar();
  }
  ImGui::End();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
  // The game advances only while its view is visible.
  const bool gameVisible = ImGui::Begin("Game", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();
  if (gameVisible) _gamePanel->draw(*this, dt);
  ImGui::End();
  if (ImGui::Begin("Hierarchy")) _hierarchy->draw(*this);
  ImGui::End();
  if (ImGui::Begin("Inspector")) _inspector->draw(*this);
  ImGui::End();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
  const bool assetsVisible = ImGui::Begin("Assets");
  ImGui::PopStyleVar();
  if (assetsVisible) _assets->draw(*this);
  ImGui::End();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
  const bool consoleVisible = ImGui::Begin("Console");
  ImGui::PopStyleVar();
  if (consoleVisible) _console->draw(*this);
  ImGui::End();
  drawAssetTabs();

  drawStatusBar();
  if (_showShortcuts) drawShortcuts();
  if (_showHistory) drawHistory();
}

void Editor::setupDockLayout(unsigned dockspace) {
  ImGui::DockBuilderRemoveNode(dockspace);
  ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::DockBuilderSetNodeSize(dockspace, {vp->WorkSize.x, vp->WorkSize.y - kToolbarHeight - kStatusHeight});
  ImGuiID center = dockspace;
  const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.17f, nullptr, &center);
  const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.27f, nullptr, &center);
  const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, nullptr, &center);
  ImGui::DockBuilderDockWindow("Scene", center);
  ImGui::DockBuilderDockWindow("Game", center);
  ImGui::DockBuilderDockWindow("Hierarchy", left);
  ImGui::DockBuilderDockWindow("Inspector", right);
  ImGui::DockBuilderDockWindow("Assets", bottom);
  ImGui::DockBuilderDockWindow("Console", bottom);
  ImGui::DockBuilderFinish(dockspace);
  focusPanel("Scene");
}

void Editor::drawMenuBar() {
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {10, 7});
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {8, 8});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10, 6});
  if (!ImGui::BeginMainMenuBar()) {
    ImGui::PopStyleVar(3);
    return;
  }
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {8, 6});
  if (ImGui::BeginMenu("File")) {
    for (const char* id : {"scene.new", "scene.open", "scene.save", "scene.saveAs"}) _commands.menuItem(id);
    ImGui::Separator();
    _commands.menuItem("project.open");
    if (ImGui::BeginMenu("   Open Recent", !recentProjects().empty())) {
      for (const RecentProject& r : recentProjects()) {
        if (ImGui::MenuItem(r.name.c_str(), r.path.c_str())) whenSaved([this, path = r.path]() { openProject(path); });
      }
      ImGui::EndMenu();
    }
    _commands.menuItem("project.close");
    ImGui::Separator();
    for (const char* id : {"project.build", "project.export", "project.settings", "project.reveal"}) _commands.menuItem(id);
    ImGui::Separator();
    _commands.menuItem("app.quit");
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Edit")) {
    // Undo/redo name what they'll do.
    const Command* undo = _commands.find("edit.undo");
    const Command* redo = _commands.find("edit.redo");
    AssetDocument* asset = activeAsset();
    const std::string undoName = asset ? asset->undoLabel() : _scene && _scene->canUndo() ? _scene->undoLabel() : "";
    const std::string redoName = asset ? asset->redoLabel() : _scene && _scene->canRedo() ? _scene->redoLabel() : "";
    const std::string undoLabel = std::string(ICON_ARROW_COUNTER_CLOCKWISE) + "  Undo" + (undoName.empty() ? "" : " " + undoName);
    const std::string redoLabel = std::string(ICON_ARROW_CLOCKWISE) + "  Redo" + (redoName.empty() ? "" : " " + redoName);
    if (ImGui::MenuItem(undoLabel.c_str(), shortcutLabel(undo->shortcut).c_str(), false, _commands.enabled(*undo))) {
      _commands.run("edit.undo");
    }
    if (ImGui::MenuItem(redoLabel.c_str(), shortcutLabel(redo->shortcut).c_str(), false, _commands.enabled(*redo))) {
      _commands.run("edit.redo");
    }
    ImGui::Separator();
    for (const char* id : {"edit.cut", "edit.copy", "edit.paste"}) _commands.menuItem(id);
    ImGui::Separator();
    for (const char* id : {"edit.duplicate", "edit.delete", "edit.rename"}) _commands.menuItem(id);
    ImGui::Separator();
    for (const char* id : {"edit.selectAll", "edit.deselect"}) _commands.menuItem(id);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Create")) {
    for (const char* kind : {"Empty", "Sprite", "Text", "Tile Map", "UI Screen", "Sound", "Script"}) {
      _commands.menuItem(std::string("create.") + kind);
    }
    ImGui::Separator();
    _commands.menuItem("prefab.make");
    _commands.menuItem("prefab.apply");
    _commands.menuItem("prefab.back");
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("View")) {
    _commands.menuItem("view.palette");
    ImGui::Separator();
    for (const char* id : {"view.frame", "view.frameAll", "view.actualSize"}) _commands.menuItem(id);
    ImGui::Separator();
    _commands.menuItem("view.grid", _scenePanel->showGrid());
    _commands.menuItem("view.snap", _scenePanel->snap());
    _commands.menuItem("view.colliders", _scenePanel->showColliders());
    _commands.menuItem("view.ui", _scenePanel->showUi());
    ImGui::Separator();
    for (const char* panel : {"Scene", "Game", "Hierarchy", "Inspector", "Assets", "Console"}) {
      _commands.menuItem(std::string("view.panel.") + panel);
    }
    _commands.menuItem("view.history", _showHistory);
    ImGui::Separator();
    _commands.menuItem("view.layout");
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Play")) {
    _commands.menuItem("play.toggle", playing());
    _commands.menuItem("play.game");
    _commands.menuItem("play.scene");
    ImGui::Separator();
    _commands.menuItem("play.pause", _paused);
    _commands.menuItem("play.step");
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Help")) {
    _commands.menuItem("help.shortcuts");
    _commands.menuItem("view.palette");
    ImGui::EndMenu();
  }
  ImGui::PopStyleVar();
  ImGui::EndMainMenuBar();
  ImGui::PopStyleVar(3);
}

void Editor::drawToolbar() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize({vp->WorkSize.x, kToolbarHeight});
  ImGui::SetNextWindowViewport(vp->ID);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10, 7});
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {2, 0});
  ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::bg0);
  ImGui::Begin("##toolbar", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar);
  ImGui::PopStyleColor();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 wp = ImGui::GetWindowPos();
  if (playing()) {
    // Play mode reads at a glance: a warm wash and an accent line under the bar.
    draw->AddRectFilled(wp, {wp.x + vp->WorkSize.x, wp.y + kToolbarHeight}, theme::u32(theme::accent, 0.07f));
    draw->AddLine({wp.x, wp.y + kToolbarHeight - 1}, {wp.x + vp->WorkSize.x, wp.y + kToolbarHeight - 1},
                  theme::u32(theme::accent, 0.8f), 2.0f);
  }
  const float h = 30.0f;

  // Scene switcher.
  {
    const std::string title = _scene ? _scene->title() : std::string("No scene");
    const std::string label = std::string(_scene && _scene->isPrefab() ? ICON_CUBE : ICON_FILM_SLATE) + "  " + title +
                              (_scene && _scene->dirty() ? "  " ICON_DOT_OUTLINE : "") + "  " + ICON_CARET_DOWN;
    ImGui::PushStyleColor(ImGuiCol_Button, theme::bg2);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::bg3);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {10, 6});
    ImGui::PushFont(theme::fonts().medium, 0.0f);
    if (ImGui::Button(label.c_str(), {0, h})) ImGui::OpenPopup("scenes");
    ImGui::PopFont();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ui::tooltip("Switch scene", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_O);
    ImGui::SetNextWindowSizeConstraints({240, 0}, {420, 480});
    if (ImGui::BeginPopup("scenes")) {
      ui::sectionLabel("Scenes");
      for (const std::string& path : _project->scenes()) {
        const bool current = _scene && _scene->path() == path;
        const std::string item = std::string(ICON_FILM_SLATE) + "  " + std::filesystem::path(path).filename().string();
        if (ImGui::MenuItem(item.c_str(), nullptr, current)) openScene(path);
      }
      ImGui::Separator();
      _commands.menuItem("scene.new");
      ImGui::EndPopup();
    }
  }

  // Tools.
  ImGui::SameLine(0, 18);
  const bool tileMap = hasTileMapSelected(*this);
  if (!tileMap && isTileTool(_tool)) _tool = Tool::Move;
  auto toolButton = [&](const char* id) {
    const Command* c = _commands.find(id);
    const bool active = c->id == toolCommand(_tool);
    ImGui::BeginDisabled(!_commands.enabled(*c));
    if (ui::iconButton(id, c->icon, c->label.c_str(), active, c->shortcut, h)) _commands.run(id);
    ImGui::EndDisabled();
    ImGui::SameLine();
  };
  // A segmented group: one rounded strip behind the buttons.
  auto group = [&](std::initializer_list<const char*> ids) {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ids.size() * h + (ids.size() - 1) * 2.0f;
    draw->AddRectFilled({start.x - 3, start.y - 3}, {start.x + width + 3, start.y + h + 3}, theme::u32(theme::bg2), theme::radius + 2);
    for (const char* id : ids) toolButton(id);
  };
  group({"tool.select", "tool.move", "tool.rotate", "tool.scale", "tool.pan"});
  if (tileMap) {
    ImGui::SameLine(0, 14);
    group({"tool.tileBrush", "tool.tileRect", "tool.tileFill", "tool.tileErase", "tool.tilePick"});
  }

  // Play controls, centered.
  {
    const float width = 3 * h + h * 0.6f + 2 * 2 + 6;
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + 12, (vp->WorkSize.x - width) * 0.5f));
    const ImVec2 start = ImGui::GetCursorScreenPos();
    draw->AddRectFilled({start.x - 3, start.y - 3}, {start.x + width + 3, start.y + h + 3}, theme::u32(theme::bg2), theme::radius + 2);
    ImGui::PushFont(theme::fonts().iconFill, 0.0f);
    const bool waiting = _playAfterBuild && _cli.busy();
    if (ui::iconButton("play", playing() ? ICON_STOP : ICON_PLAY, playing() ? "Stop" : "Play", playing() || waiting,
                       ImGuiMod_Ctrl | ImGuiKey_P, h)) {
      _commands.run("play.toggle");
    }
    ImGui::PopFont();
    ImGui::SameLine(0, 0);
    {
      // A narrow, full-height caret: Play Scene / Play Game.
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float w = h * 0.6f;
      if (ImGui::InvisibleButton("##playmenu", {w, h})) ImGui::OpenPopup("play menu");
      if (ImGui::IsItemHovered()) draw->AddRectFilled(p, {p.x + w, p.y + h}, theme::u32(theme::text, 0.07f), theme::radius);
      ImGui::PushFont(nullptr, theme::sizeSmall);
      const ImVec2 cs = ImGui::CalcTextSize(ICON_CARET_DOWN);
      draw->AddText({p.x + (w - cs.x) * 0.5f, p.y + (h - cs.y) * 0.5f}, theme::u32(theme::textDim), ICON_CARET_DOWN);
      ImGui::PopFont();
      ui::tooltip("Play this scene or the whole game");
    }
    if (ImGui::BeginPopup("play menu")) {
      _commands.menuItem("play.scene");
      _commands.menuItem("play.game");
      ImGui::EndPopup();
    }
    ImGui::PushFont(theme::fonts().iconFill, 0.0f);
    ImGui::SameLine();
    ImGui::BeginDisabled(!playing());
    if (ui::iconButton("pause", ICON_PAUSE, "Pause", _paused, ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_P, h)) _commands.run("play.pause");
    ImGui::SameLine();
    if (ui::iconButton("step", ICON_SKIP_FORWARD, "Step one frame", false, ImGuiKey_F10, h)) _commands.run("play.step");
    ImGui::EndDisabled();
    ImGui::PopFont();
  }

  // Right side: build status, export, palette.
  {
    const float paletteWidth = 220.0f;
    const float exportWidth = 92.0f;
    const float right = vp->WorkSize.x - 10.0f;
    ImGui::SameLine();
    ImGui::SetCursorPosX(right - paletteWidth);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##palette", {paletteWidth, h})) openPalette();
    const bool hovered = ImGui::IsItemHovered();
    draw->AddRectFilled(p, {p.x + paletteWidth, p.y + h}, theme::u32(hovered ? theme::bg3 : theme::bg2), h * 0.5f);
    draw->AddText({p.x + 12, p.y + (h - ImGui::GetTextLineHeight()) * 0.5f}, theme::u32(theme::textFaint),
                  ICON_MAGNIFYING_GLASS "  Search commands");
    const std::string keys = shortcutLabel(ImGuiMod_Ctrl | ImGuiKey_K);
    const ImVec2 ks = ImGui::CalcTextSize(keys.c_str());
    draw->AddText({p.x + paletteWidth - ks.x - 14, p.y + (h - ks.y) * 0.5f}, theme::u32(theme::textFaint), keys.c_str());

    ImGui::SameLine();
    ImGui::SetCursorPosX(right - paletteWidth - exportWidth - 10);
    if (ui::button(ICON_PACKAGE "  Export", {exportWidth, h})) _export->open();
    ui::tooltip("Export a standalone game", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_E);

    // Build status chip.
    ImGui::SameLine();
    const float chipWidth = 150.0f;
    ImGui::SetCursorPosX(right - paletteWidth - exportWidth - 20 - chipWidth);
    const ImVec2 c = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##build", {chipWidth, h})) {
      if (_cli.busy() || _lastBuildFailed) focusPanel("Console");
      if (!_cli.busy()) build();
    }
    const bool chipHovered = ImGui::IsItemHovered();
    if (chipHovered) draw->AddRectFilled(c, {c.x + chipWidth, c.y + h}, theme::u32(theme::bg2), theme::radius);
    const float ty = c.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
    if (_cli.busy()) {
      ImGui::SetCursorScreenPos({c.x + 8, c.y + (h - 14) * 0.5f});
      ui::spinner(7, theme::u32(theme::accent));
      char text[64];
      std::snprintf(text, sizeof(text), "%s  %.1fs", _cli.label() == "Export" ? "Exporting" : "Building", _cli.elapsed());
      draw->AddText({c.x + 28, ty}, theme::u32(theme::textDim), text);
    } else if (_lastBuildFailed) {
      draw->AddText({c.x + 8, ty}, theme::u32(theme::error), ICON_WARNING_OCTAGON "  Build failed");
    } else if (_buildStale) {
      draw->AddText({c.x + 8, ty}, theme::u32(theme::warning), ICON_CIRCLE_DASHED "  Changes pending");
    } else {
      draw->AddText({c.x + 8, ty}, theme::u32(theme::textFaint), ICON_CHECK_CIRCLE "  Up to date");
    }
    if (chipHovered) {
      ImGui::SetTooltip("%s", _cli.busy() ? "Show the build output" : _lastBuildFailed ? "See why in the Console, or build again" : "Build now");
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
}

void Editor::drawStatusBar() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos({vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - kStatusHeight});
  ImGui::SetNextWindowSize({vp->WorkSize.x, kStatusHeight});
  ImGui::SetNextWindowViewport(vp->ID);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12, 4});
  ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::bg0);
  ImGui::Begin("##status", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar);
  ImGui::PopStyleColor();
  ImGui::PushFont(nullptr, theme::sizeSmall);
  ImGui::AlignTextToFramePadding();

  ImGui::TextColored(theme::textDim, ICON_FOLDER_SIMPLE " %s", _project->name().c_str());
  if (playing()) {
    ImGui::SameLine(0, 16);
    ImGui::TextColored(theme::accent, _paused ? ICON_PAUSE " Paused" : ICON_PLAY " Playing");
    if (_gameFocused) {
      ImGui::SameLine(0, 10);
      ImGui::TextColored(theme::textFaint, "Game has the keyboard. Click outside it to use shortcuts.");
    }
  }

  // Right-aligned: selection, cursor, zoom, problems.
  const auto counts = LogBook::instance().counts();
  char right[256];
  const glm::vec2 cursor = _scenePanel->cursorWorld();
  const std::string selected = _selection.empty() ? std::string() : std::to_string(_selection.size()) + " selected   ";
  std::snprintf(right, sizeof(right), "%s" ICON_CROSSHAIR_SIMPLE " %.0f, %.0f   " ICON_MAGNIFYING_GLASS " %d%%",
                selected.c_str(), cursor.x, cursor.y, static_cast<int>(_scenePanel->zoom() * 100 + 0.5f));
  const float problemsWidth = 96.0f;
  const float rw = ImGui::CalcTextSize(right).x;
  ImGui::SameLine(vp->WorkSize.x - rw - problemsWidth - 24);
  ImGui::TextColored(theme::textFaint, "%s", right);
  ImGui::SameLine(vp->WorkSize.x - problemsWidth - 12);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  if (ImGui::InvisibleButton("##problems", {problemsWidth, ImGui::GetFrameHeight()})) focusPanel("Console");
  ui::tooltip("Show the console");
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const float ty = p.y + (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f;
  char errors[32], warnings[32];
  std::snprintf(errors, sizeof(errors), ICON_WARNING_OCTAGON " %d", counts[2]);
  std::snprintf(warnings, sizeof(warnings), ICON_WARNING " %d", counts[1]);
  draw->AddText({p.x, ty}, theme::u32(counts[2] ? theme::error : theme::textFaint), errors);
  draw->AddText({p.x + 48, ty}, theme::u32(counts[1] ? theme::warning : theme::textFaint), warnings);
  ImGui::PopFont();
  ImGui::End();
  ImGui::PopStyleVar();
}

void Editor::drawSavePrompt() {
  if (_askSave) {
    ImGui::OpenPopup("Unsaved changes");
    _askSave = false;
  }
  ui::centerNextWindow({420, 0});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {20, 18});
  if (ImGui::BeginPopupModal("Unsaved changes", nullptr,
                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::PushFont(theme::fonts().semibold, theme::sizeTitle);
    ImGui::Text("Save changes to \"%s\"?", _scene ? _scene->title().c_str() : "");
    ImGui::PopFont();
    ImGui::Dummy({0, 2});
    ui::dimText("Your changes will be lost if you don't save them.");
    ImGui::Dummy({0, 10});
    const float bw = 96.0f;
    if (ui::button("Don't Save", {bw + 8, 0})) {
      ImGui::CloseCurrentPopup();
      if (_scene) {
        std::error_code ec;
        std::filesystem::remove(recoveryFile(), ec);
        // Discarding: reload the file, so the next action sees a clean document.
        std::string error;
        if (_project && _project->file(_scene->path())) {
          if (auto doc = SceneDocument::load(*_project, _scene->path(), error)) _scene = std::move(doc);
        } else {
          _scene.reset();
        }
      }
      runAfterSave();
    }
    ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 2 * bw - 8);
    if (ui::button("Cancel", {bw, 0}) || ui::dismissPressed()) {
      _afterSave = nullptr;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine(0, 8);
    if (ui::primaryButton("Save", {bw, 0}) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
      ImGui::CloseCurrentPopup();
      if (saveScene()) {
        runAfterSave();
      } else {
        _afterSave = nullptr;
      }
    }
    ImGui::EndPopup();
  }
  ImGui::PopStyleVar();
}

void Editor::drawHistory() {
  ImGui::SetNextWindowSize({280, 360}, ImGuiCond_FirstUseEver);
  if (ImGui::Begin(ICON_CLOCK_COUNTER_CLOCKWISE "  History###History", &_showHistory) && _scene) {
    const auto labels = _scene->historyLabels();
    const size_t at = _scene->historyPosition();
    // Row 0 is the document as opened; row n is after step n.
    auto row = [&](size_t position, const std::string& label) {
      const bool current = position == at;
      const bool undone = position > at;
      ImGui::PushStyleColor(ImGuiCol_Text, undone ? theme::textFaint : current ? theme::text : theme::textDim);
      ImGui::PushID(static_cast<int>(position));
      if (ImGui::Selectable(label.c_str(), current)) _scene->jumpTo(position);
      ImGui::PopID();
      ImGui::PopStyleColor();
    };
    row(0, std::string(ICON_FILE) + "  Opened " + _scene->title());
    for (size_t i = 0; i < labels.size(); ++i) row(i + 1, labels[i]);
    if (labels.empty()) {
      ImGui::Dummy({0, 6});
      ui::dimText("Changes you make appear here.");
    }
  }
  ImGui::End();
}

void Editor::drawPrompt() {
  if (!_prompt) return;
  if (_prompt->opening) {
    ImGui::OpenPopup("##prompt");
    _prompt->opening = false;
  }
  ui::centerNextWindow({400, 0});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {20, 18});
  if (ImGui::BeginPopupModal("##prompt", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::PushFont(theme::fonts().semibold, theme::sizeTitle);
    ImGui::TextUnformatted(_prompt->title.c_str());
    ImGui::PopFont();
    ImGui::Dummy({0, 6});
    ui::smallText(_prompt->label.c_str(), theme::textDim);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(360);
    const bool enter = ImGui::InputText("##text", &_prompt->text, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    // File names: no separators or leading dots.
    std::string text = _prompt->text;
    text.erase(0, text.find_first_not_of(" ."));
    text.erase(text.find_last_not_of(' ') + 1);
    const bool valid = !text.empty() && text.find_first_of("/\\:*?\"<>|") == std::string::npos;
    if (!valid && !_prompt->text.empty()) ui::smallText(ICON_WARNING " Use a plain name, without / \\ : * ? \" < > |", theme::warning);
    else if (valid && _prompt->where) ui::smallText(("Saves as " + _prompt->where(text)).c_str(), theme::textFaint);
    ImGui::Dummy({0, 8});
    const float bw = 96.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 360 - 2 * bw - 8);
    const bool cancel = ui::button("Cancel", {bw, 0}) || ui::dismissPressed();
    ImGui::SameLine(0, 8);
    ImGui::BeginDisabled(!valid);
    // The button says what happens: "New Script" creates, "Rename Group" renames, "Save As" saves.
    const std::string verb = _prompt->title.starts_with("New ") ? "Create" : _prompt->title.substr(0, _prompt->title.find(' '));
    const bool ok = ui::primaryButton(verb.c_str(), {bw, 0}) || (enter && valid);
    ImGui::EndDisabled();
    if (ok || cancel) {
      ImGui::CloseCurrentPopup();
      auto done = std::move(_prompt->done);
      _prompt.reset();
      if (ok) done(text);
    }
    ImGui::EndPopup();
  }
  ImGui::PopStyleVar();
}

void Editor::drawShortcuts() {
  ImGui::OpenPopup("Keyboard Shortcuts");
  ui::centerNextWindow({620, 560});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {20, 16});
  bool open = true;
  if (ImGui::BeginPopupModal("Keyboard Shortcuts", &open, ImGuiWindowFlags_NoResize)) {
    std::string category;
    if (ImGui::BeginTable("shortcuts", 2, ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
      for (const Command& c : _commands.all()) {
        if (!c.shortcut || c.id == "edit.deleteBack" || c.id == "view.palette2") continue;
        if (c.category != category) {
          category = c.category;
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::Dummy({0, 4});
          ui::sectionLabel(category.c_str());
        }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%s  %s", c.icon ? c.icon : "", c.label.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored(theme::textDim, "%s", shortcutLabel(c.shortcut).c_str());
      }
      ImGui::EndTable();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) open = false;
    ImGui::EndPopup();
  }
  ImGui::PopStyleVar();
  if (!open) _showShortcuts = false;
}
