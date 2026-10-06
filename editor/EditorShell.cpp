// The workspace around the panels: commands, menu bar, toolbar, dock layout,
// status bar and the unsaved-changes prompt.

#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <nfd.hpp>

#include "Editor.hpp"
#include "Icons.hpp"
#include "LogBook.hpp"
#include "Theme.hpp"
#include "Ui.hpp"
#include "panels/Panels.hpp"

namespace {

constexpr float kToolbarHeight = 44.0f;
constexpr float kStatusHeight = 26.0f;

constexpr std::pair<const char*, const char*> kCreateKinds[] = {
    {"Empty", ICON_CUBE_TRANSPARENT}, {"Sprite", ICON_IMAGE}, {"Text", ICON_TEXT_T},   {"Tile Map", ICON_GRID_FOUR},
    {"UI Screen", ICON_BROWSER},      {"Sound", ICON_SPEAKER_HIGH}, {"Script", ICON_CODE}};

constexpr std::pair<const char*, ImGuiKey> kPanels[] = {{"Scene", ImGuiKey_1},     {"Game", ImGuiKey_2},
                                                        {"Hierarchy", ImGuiKey_3}, {"Inspector", ImGuiKey_4},
                                                        {"Assets", ImGuiKey_5},    {"Console", ImGuiKey_6}};

struct ToolInfo {
  Tool tool;
  const char* id;
  const char* label;
  const char* icon;
  ImGuiKey key;
};
constexpr ToolInfo kTools[] = {
    {Tool::Select, "tool.select", "Select Tool", ICON_CURSOR, ImGuiKey_Q},
    {Tool::Move, "tool.move", "Move Tool", ICON_ARROWS_OUT_CARDINAL, ImGuiKey_W},
    {Tool::Rotate, "tool.rotate", "Rotate Tool", ICON_ARROWS_CLOCKWISE, ImGuiKey_E},
    {Tool::Scale, "tool.scale", "Scale Tool", ICON_ARROWS_OUT, ImGuiKey_R},
    {Tool::Pan, "tool.pan", "Hand Tool", ICON_HAND, ImGuiKey_H},
    {Tool::TileBrush, "tool.tileBrush", "Tile Brush", ICON_PAINT_BRUSH_BROAD, ImGuiKey_B},
    {Tool::TileRect, "tool.tileRect", "Tile Rectangle", ICON_RECTANGLE, ImGuiKey_U},
    {Tool::TileFill, "tool.tileFill", "Tile Fill", ICON_PAINT_BUCKET, ImGuiKey_G},
    {Tool::TileErase, "tool.tileErase", "Tile Eraser", ICON_ERASER, ImGuiKey_X},
    {Tool::TilePick, "tool.tilePick", "Tile Picker", ICON_EYEDROPPER, ImGuiKey_I}};

bool hasTileMapSelected(Editor& e) {
  return e.scene() && e.primary() && e.preview().tileGrid(e.primary()) != nullptr;
}

// A fixed, undecorated window on the ground color (the toolbar, status bar and dock host).
void beginBar(const char* id, ImVec2 pos, ImVec2 size, ImGuiWindowFlags extra = 0) {
  ImGui::SetNextWindowPos(pos);
  ImGui::SetNextWindowSize(size);
  ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::bg0);
  ImGui::Begin(id, nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | extra);
  ImGui::PopStyleColor();
}

// A small titled modal; true while it's open (then EndPopup).
bool beginDialog(const char* id, float width, const std::string& title) {
  ui::centerNextWindow({width, 0});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {20, 18});
  const bool open = ImGui::BeginPopupModal(id, nullptr,
                                           ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::PopStyleVar();
  if (!open) return false;
  ImGui::PushFont(theme::fonts().semibold, theme::sizeTitle);
  ImGui::TextUnformatted(title.c_str());
  ImGui::PopFont();
  return true;
}

}  // namespace

void Editor::registerCommands() {
  auto hasProject = [this]() { return _project.has_value(); };
  auto hasScene = [this]() { return _scene.has_value(); };
  auto hasSelection = [this]() { return _scene && !_selection.empty(); };
  auto editsScene = [this]() { return _scene && !_scene->isPrefab(); };

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
                 }, [editsScene, hasSelection]() { return editsScene() && hasSelection(); }});
  _commands.add({"prefab.back", "Back to Scene", "Prefab", ICON_ARROW_LEFT, 0, [this]() { returnFromPrefab(); },
                 [this]() { return !returnScene().empty() && _scene && _scene->isPrefab(); }});
  _commands.add({"prefab.apply", "Apply Overrides to Prefab", "Prefab", ICON_UPLOAD_SIMPLE, 0, [this]() { applyOverrides(primary()); },
                 [this]() { const Json* e = _scene ? _scene->find(primary()) : nullptr; return e && e->contains("prefab"); }});
  _commands.add({"scene.saveAs", "Save As...", "File", ICON_FLOPPY_DISK_BACK, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S,
                 [this]() { saveSceneAs(); }, hasScene});
  _commands.add({"project.build", "Build", "File", ICON_HAMMER, ImGuiMod_Ctrl | ImGuiKey_B, [this]() { build(); },
                 [this]() { return _project && !_cli.busy(); }});
  _commands.add({"project.cancelBuild", "Cancel Build", "File", ICON_X_CIRCLE, 0, [this]() { _cli.cancel(); },
                 [this]() { return _cli.busy(); }});
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
  _commands.add({"edit.paste", "Paste", "Edit", ICON_CLIPBOARD, ImGuiMod_Ctrl | ImGuiKey_V, [this]() { paste(); }, hasScene});
  // An asset tab in use takes `tabId` for what's selected in it; otherwise it acts on the scene's selection.
  auto addTabbed = [&](const char* id, const char* tabId, const char* label, const char* icon, ImGuiKeyChord key,
                       std::function<void()> onScene) {
    _commands.add({id, label, "Edit", icon, key, [this, tabId, onScene]() { if (!assetCommand(tabId, true)) onScene(); },
                   [this, tabId, hasSelection]() { return assetCommand(tabId, false) || hasSelection(); }});
  };
  addTabbed("edit.duplicate", "edit.duplicate", "Duplicate", ICON_COPY, ImGuiMod_Ctrl | ImGuiKey_D, [this]() { duplicateSelection(); });
  addTabbed("edit.delete", "edit.delete", "Delete", ICON_TRASH, ImGuiKey_Delete, [this]() { deleteSelection(); });
  addTabbed("edit.deleteBack", "edit.delete", "Delete", ICON_TRASH, ImGuiKey_Backspace, [this]() { deleteSelection(); });
  _commands.add({"edit.rename", "Rename", "Edit", ICON_PENCIL_SIMPLE, ImGuiKey_F2, [this]() {
                   _hierarchy->rename(primary());
                   focusPanel("Hierarchy");
                 }, [this]() { return _scene && _selection.size() == 1 && !(_scene->isPrefab() && _scene->indexOf(primary()) == 0); }});
  _commands.add({"edit.selectAll", "Select All", "Edit", ICON_SELECTION_ALL, ImGuiMod_Ctrl | ImGuiKey_A, [this]() {
                   std::vector<EntityUid> all;
                   for (size_t i = 0; i < _scene->size(); ++i) all.push_back(_scene->uid(i));
                   selectAll(all);
                 }, hasScene});
  _commands.add({"edit.deselect", "Deselect", "Edit", ICON_SELECTION_SLASH, ImGuiKey_Escape, [this]() { _selection.clear(); },
                 hasSelection});

  // Create
  for (const auto& [kind, icon] : kCreateKinds) {
    _commands.add({std::string("create.") + kind, std::string("Create ") + kind, "Create", icon, 0,
                   [this, kind]() { createEntity(kind, _scenePanel->viewCenter()); }, hasScene});
  }

  // View and tools
  auto addToggle = [&](const char* id, const char* label, const char* icon, ImGuiKeyChord key, std::function<bool&()> flag) {
    _commands.add({id, label, "View", icon, key, [flag]() { flag() = !flag(); }, hasScene, false, [flag]() { return flag(); }});
  };
  _commands.add({"view.frame", "Frame Selection", "View", ICON_CORNERS_OUT, ImGuiKey_F, [this]() { _scenePanel->frameSelection(*this); },
                 hasScene});
  _commands.add({"view.frameAll", "Frame Scene", "View", ICON_FRAME_CORNERS, ImGuiKey_Home, [this]() { _scenePanel->frameAll(*this); },
                 hasScene});
  _commands.add({"view.actualSize", "Zoom to 100%", "View", ICON_MAGNIFYING_GLASS, ImGuiMod_Ctrl | ImGuiKey_0,
                 [this]() { _scenePanel->setZoom(1.0f); }, hasScene});
  addToggle("view.grid", "Toggle Grid", ICON_GRID_NINE, ImGuiMod_Ctrl | ImGuiKey_Apostrophe,
            [this]() -> bool& { return _scenePanel->showGrid(); });
  addToggle("view.snap", "Toggle Snapping", ICON_MAGNET, ImGuiMod_Shift | ImGuiKey_G, [this]() -> bool& { return _scenePanel->snap(); });
  addToggle("view.colliders", "Toggle Collider Outlines", ICON_BOUNDING_BOX, 0,
            [this]() -> bool& { return _scenePanel->showColliders(); });
  addToggle("view.ui", "Toggle Game UI in Scene", ICON_BROWSER, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_U,
            [this]() -> bool& { return _scenePanel->showUi(); });
  _commands.add({"view.palette", "Command Palette...", "View", ICON_COMMAND, ImGuiMod_Ctrl | ImGuiKey_K, [this]() { openPalette(); }});
  _commands.add({"view.palette2", "Command Palette...", "View", ICON_COMMAND, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P,
                 [this]() { openPalette(); }});
  _commands.add({"view.layout", "Reset Layout", "View", ICON_LAYOUT, 0, [this]() { _resetLayout = true; }, hasProject});
  addToggle("view.history", "Undo History", ICON_CLOCK_COUNTER_CLOCKWISE, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_H,
            [this]() -> bool& { return _showHistory; });
  for (const auto& [panel, key] : kPanels) {
    _commands.add({std::string("view.panel.") + panel, std::string("Show ") + panel, "View", ICON_APP_WINDOW,
                   ImGuiMod_Ctrl | key, [this, panel]() { focusPanel(panel); }, hasProject});
  }
  for (const ToolInfo& t : kTools) {
    _commands.add({t.id, t.label, "Tools", t.icon, t.key, [this, tool = t.tool]() { _tool = tool; },
                   isTileTool(t.tool) ? std::function<bool()>([this]() { return hasTileMapSelected(*this); }) : hasScene});
  }

  // Play
  auto isPlaying = [this]() { return playing(); };
  _commands.add({"play.toggle", "Play / Stop", "Play", ICON_PLAY, ImGuiMod_Ctrl | ImGuiKey_P,
                 [this]() { playing() ? stopPlay() : startPlay(); }, hasScene, true, isPlaying});
  _commands.add({"play.game", "Play Game (from the first scene)", "Play", ICON_GAME_CONTROLLER, ImGuiKey_F5,
                 [this]() { playing() ? stopPlay() : startPlay(PlayFrom::Game); }, hasProject, true});
  _commands.add({"play.scene", "Play This Scene", "Play", ICON_PLAY, ImGuiKey_F6,
                 [this]() { playing() ? stopPlay() : startPlay(PlayFrom::Scene); }, hasScene, true});
  _commands.add({"play.pause", "Pause / Resume", "Play", ICON_PAUSE, ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_P,
                 [this]() { togglePause(); }, isPlaying, true, [this]() { return _paused; }});
  _commands.add({"play.step", "Step One Frame", "Play", ICON_SKIP_FORWARD, ImGuiKey_F10, [this]() { stepFrame(); }, isPlaying, true});

  // Help
  _commands.add({"help.shortcuts", "Keyboard Shortcuts", "Help", ICON_KEYBOARD, ImGuiMod_Ctrl | ImGuiKey_Slash,
                 [this]() { _showShortcuts = true; }});
}

void Editor::drawWorkspace(float dt) {
  drawMenuBar();
  drawToolbar();

  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {4, 0});
  beginBar("##dockhost", {vp->WorkPos.x, vp->WorkPos.y + kToolbarHeight},
           {vp->WorkSize.x, vp->WorkSize.y - kToolbarHeight - kStatusHeight}, ImGuiWindowFlags_NoNavFocus);
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
  // Begins a panel whose content runs to its edges.
  auto beginFlush = [](const char* name, ImGuiWindowFlags flags = 0) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    const bool visible = ImGui::Begin(name, nullptr, flags);
    ImGui::PopStyleVar();
    return visible;
  };
  constexpr ImGuiWindowFlags kNoScroll = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  if (beginFlush("Scene", kNoScroll)) _scenePanel->draw(*this, dt);
  ImGui::End();
  // The game advances only while its view is visible.
  if (beginFlush("Game", kNoScroll)) _gamePanel->draw(*this, dt);
  ImGui::End();
  if (ImGui::Begin("Hierarchy")) _hierarchy->draw(*this);
  ImGui::End();
  if (ImGui::Begin("Inspector")) _inspector->draw(*this);
  ImGui::End();
  if (beginFlush("Assets")) _assets->draw(*this);
  ImGui::End();
  if (beginFlush("Console")) _console->draw(*this);
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
    _commands.menuItems({"scene.new", "scene.open", "scene.save", "scene.saveAs", "", "project.open"});
    if (ImGui::BeginMenu(ICON_CLOCK_COUNTER_CLOCKWISE "  Open Recent", !recentProjects().empty())) {
      for (const RecentProject& r : recentProjects()) {
        if (ImGui::MenuItem(r.name.c_str(), r.path.c_str())) whenSaved([this, path = r.path]() { openProject(path); });
      }
      ImGui::EndMenu();
    }
    _commands.menuItems({"project.close", "", "project.build", "project.export", "project.settings", "project.reveal", "", "app.quit"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Edit")) {
    // Undo/redo name what they'll do.
    AssetDocument* asset = activeAsset();
    _commands.menuItem("edit.undo", asset ? asset->undoLabel() : _scene ? _scene->undoLabel() : "");
    _commands.menuItem("edit.redo", asset ? asset->redoLabel() : _scene ? _scene->redoLabel() : "");
    _commands.menuItems({"", "edit.cut", "edit.copy", "edit.paste", "", "edit.duplicate", "edit.delete", "edit.rename", "",
                         "edit.selectAll", "edit.deselect"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Create")) {
    for (const auto& kind : kCreateKinds) _commands.menuItem(std::string("create.") + kind.first);
    _commands.menuItems({"", "prefab.make", "prefab.apply", "prefab.back"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("View")) {
    _commands.menuItems({"view.palette", "", "view.frame", "view.frameAll", "view.actualSize", "", "view.grid", "view.snap",
                         "view.colliders", "view.ui", ""});
    for (const auto& panel : kPanels) _commands.menuItem(std::string("view.panel.") + panel.first);
    _commands.menuItems({"view.history", "", "view.layout"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Play")) {
    _commands.menuItems({"play.toggle", "play.game", "play.scene", "", "play.pause", "play.step"});
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Help")) {
    _commands.menuItems({"help.shortcuts", "view.palette"});
    ImGui::EndMenu();
  }
  ImGui::PopStyleVar();
  ImGui::EndMainMenuBar();
  ImGui::PopStyleVar(3);
}

void Editor::drawToolbar() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10, 7});
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {2, 0});
  beginBar("##toolbar", vp->WorkPos, {vp->WorkSize.x, kToolbarHeight});
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 wp = ImGui::GetWindowPos();
  if (playing()) {
    // Play mode reads at a glance: a warm wash and an accent line under the bar.
    draw->AddRectFilled(wp, {wp.x + vp->WorkSize.x, wp.y + kToolbarHeight}, theme::u32(theme::accent, 0.07f));
    draw->AddLine({wp.x, wp.y + kToolbarHeight - 1}, {wp.x + vp->WorkSize.x, wp.y + kToolbarHeight - 1},
                  theme::u32(theme::accent, 0.8f), 2.0f);
  }
  const float h = 30.0f;
  auto keys = [this](const char* id) { return _commands.find(id)->shortcut; };
  // A rounded strip behind a group of buttons.
  auto strip = [&](float width) {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    draw->AddRectFilled({start.x - 3, start.y - 3}, {start.x + width + 3, start.y + h + 3}, theme::u32(theme::bg2), theme::radius + 2);
  };
  // A command's icon button, disabled with it; `tip` overrides its label.
  auto commandButton = [&](const char* id, bool active, const char* tip = nullptr) {
    const Command* c = _commands.find(id);
    ImGui::BeginDisabled(!_commands.enabled(*c));
    if (ui::iconButton(id, c->icon, tip ? tip : c->label.c_str(), active, c->shortcut, h)) _commands.run(id);
    ImGui::EndDisabled();
    ImGui::SameLine();
  };

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
    ui::tooltip("Switch scene", keys("scene.open"));
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

  // Tools: the transform group, then the tile group while a tile map is selected.
  ImGui::SameLine(0, 18);
  const bool tileMap = hasTileMapSelected(*this);
  if (!tileMap && isTileTool(_tool)) _tool = Tool::Move;
  for (const bool tiles : {false, true}) {
    if (tiles && !tileMap) break;
    if (tiles) ImGui::SameLine(0, 14);
    const auto count = static_cast<float>(
        std::count_if(std::begin(kTools), std::end(kTools), [&](const ToolInfo& t) { return isTileTool(t.tool) == tiles; }));
    strip(count * h + (count - 1) * 2.0f);
    for (const ToolInfo& t : kTools) {
      if (isTileTool(t.tool) == tiles) commandButton(t.id, t.tool == _tool);
    }
  }

  // Play controls, centered.
  {
    const float width = 3 * h + h * 0.6f + 2 * 2 + 6;
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + 12, (vp->WorkSize.x - width) * 0.5f));
    strip(width);
    ImGui::PushFont(theme::fonts().iconFill, 0.0f);
    const bool waiting = _playAfterBuild && _cli.busy();
    if (ui::iconButton("play", playing() ? ICON_STOP : ICON_PLAY, playing() ? "Stop" : "Play", playing() || waiting,
                       keys("play.toggle"), h)) {
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
      _commands.menuItems({"play.scene", "play.game"});
      ImGui::EndPopup();
    }
    ImGui::PushFont(theme::fonts().iconFill, 0.0f);
    ImGui::SameLine();
    commandButton("play.pause", _paused, "Pause");
    commandButton("play.step", false, "Step one frame");
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
    const std::string paletteKeys = shortcutLabel(keys("view.palette"));
    const ImVec2 ks = ImGui::CalcTextSize(paletteKeys.c_str());
    draw->AddText({p.x + paletteWidth - ks.x - 14, p.y + (h - ks.y) * 0.5f}, theme::u32(theme::textFaint), paletteKeys.c_str());

    ImGui::SameLine();
    ImGui::SetCursorPosX(right - paletteWidth - exportWidth - 10);
    if (ui::button(ICON_PACKAGE "  Export", {exportWidth, h})) _export->open();
    ui::tooltip("Export a standalone game", keys("project.export"));

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
    const ImVec2 at{c.x + 8, c.y + (h - ImGui::GetTextLineHeight()) * 0.5f};
    if (_cli.busy()) {
      ImGui::SetCursorScreenPos({c.x + 8, c.y + (h - 14) * 0.5f});
      ui::spinner(7, theme::u32(theme::accent));
      char text[64];
      std::snprintf(text, sizeof(text), "%s  %.1fs", _cli.label() == "Export" ? "Exporting" : "Building", _cli.elapsed());
      draw->AddText({c.x + 28, at.y}, theme::u32(theme::textDim), text);
    } else if (_buildCancelled) {
      draw->AddText(at, theme::u32(theme::textDim), ICON_X_CIRCLE "  Build cancelled");
    } else if (_lastBuildFailed) {
      draw->AddText(at, theme::u32(theme::error), ICON_WARNING_OCTAGON "  Build failed");
    } else if (_buildStale) {
      draw->AddText(at, theme::u32(theme::warning), ICON_CIRCLE_DASHED "  Changes pending");
    } else {
      draw->AddText(at, theme::u32(theme::textFaint), ICON_CHECK_CIRCLE "  Up to date");
    }
    if (chipHovered) {
      ImGui::SetTooltip("%s", _cli.busy() ? "Show the build output (Cancel Build stops it)"
                              : _lastBuildFailed ? "See why in the Console, or build again"
                                                 : "Build now");
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
}

void Editor::drawStatusBar() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12, 4});
  beginBar("##status", {vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - kStatusHeight}, {vp->WorkSize.x, kStatusHeight});
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
  const auto counts = LogBook::instance().counts();  // by level: info, warning, error
  const float ty = p.y + (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f;
  char errors[32], warnings[32];
  std::snprintf(errors, sizeof(errors), ICON_WARNING_OCTAGON " %d", counts[2]);
  std::snprintf(warnings, sizeof(warnings), ICON_WARNING " %d", counts[1]);
  ImDrawList* draw = ImGui::GetWindowDrawList();
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
  if (beginDialog("Unsaved changes", 420, "Save changes to \"" + (_scene ? _scene->title() : "") + "\"?")) {
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
  if (beginDialog("##prompt", 400, _prompt->title)) {
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
