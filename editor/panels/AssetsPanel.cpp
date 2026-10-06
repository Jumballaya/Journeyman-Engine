// The Assets panel: the project's files as a folder tree and a thumbnail grid.
// Files drag into the scene, the hierarchy and inspector fields; atlases open
// like folders to show their regions.

#include <filesystem>

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include "Entities.hpp"
#include "Icons.hpp"
#include "editors/EditorWidgets.hpp"
#include "Panels.hpp"
#include "Theme.hpp"
#include "TiledFiles.hpp"
#include "Thumbnails.hpp"
#include "UiThumbnails.hpp"
#include "Ui.hpp"

namespace fs = std::filesystem;

namespace {

std::string parentOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

std::string nameOf(const std::string& path) {
  const size_t cut = path.find_last_of("/#");
  return cut == std::string::npos ? path : path.substr(cut + 1);
}

std::vector<std::string> subfolders(const Project& project, const std::string& parent) {
  std::vector<std::string> out;
  for (const AssetFile& f : project.files()) {
    if (f.kind == AssetKind::Folder && parentOf(f.path) == parent) out.push_back(f.path);
  }
  return out;
}

std::string sizeLabel(uintmax_t bytes) {
  char out[32];
  if (bytes < 1024) std::snprintf(out, sizeof(out), "%ju B", bytes);
  else if (bytes < 1024 * 1024) std::snprintf(out, sizeof(out), "%.1f KB", bytes / 1024.0);
  else std::snprintf(out, sizeof(out), "%.1f MB", bytes / (1024.0 * 1024.0));
  return out;
}

ImVec4 kindColor(AssetKind kind) {
  switch (kind) {
    case AssetKind::Scene: return theme::accent;
    case AssetKind::Prefab: return theme::info;
    case AssetKind::Script: return theme::warning;
    case AssetKind::Sound: return theme::success;
    case AssetKind::Ui:
    case AssetKind::Style: return ImVec4(0.78f, 0.55f, 0.95f, 1.0f);
    default: return theme::textDim;
  }
}

// What a tile shows: an image or region itself, a screen's render, a prefab's
// or tileset's picture, an atlas's first region; nothing (its kind's icon) else.
std::optional<Thumbnails::Picture> tilePicture(Editor& editor, const AssetFile& item) {
  const Project& project = *editor.project();
  Thumbnails& thumbnails = Thumbnails::instance();
  if (item.kind == AssetKind::Image || item.path.find('#') != std::string::npos) return thumbnails.get(project, item.path);
  switch (item.kind) {
    case AssetKind::Ui:
      return UiThumbnails::instance().get(project, item.path, editor.buildGeneration());
    case AssetKind::Prefab: {
      const std::string image = prefabImage(project, item.path);
      return image.empty() ? std::nullopt : thumbnails.get(project, image);
    }
    case AssetKind::Tileset: {
      const Json* tileset = editor.tileset(item.path);
      const auto ids = tileset ? tiled::tileIds(*tileset) : std::vector<uint32_t>{};
      return ids.empty() ? std::nullopt : widgets::tilePicture(project, *tileset, item.path, ids.front());
    }
    case AssetKind::Atlas: {
      const auto regions = thumbnails.regions(project, item.path);
      return regions.empty() ? std::nullopt : thumbnails.get(project, item.path + "#" + regions.front());
    }
    default:
      return std::nullopt;
  }
}

}  // namespace

void AssetsPanel::reveal(const std::string& path) {
  _folder = parentOf(path.substr(0, path.find('#')));
  _selected = path;
  _filter.clear();
}

void AssetsPanel::drawFolderTree(Editor& editor, const std::string& folder, int depth) {
  // By value: a drop moves files, rescanning the project's list under the loop.
  for (const std::string& path : subfolders(*editor.project(), folder)) {
    const bool current = _folder == path;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding;
    if (subfolders(*editor.project(), path).empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (current) flags |= ImGuiTreeNodeFlags_Selected;
    if (_folder.starts_with(path + "/") || depth == 0) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    const std::string label = std::string(current ? ICON_FOLDER_OPEN : ICON_FOLDER_SIMPLE) + "  " + nameOf(path);
    const bool open = ImGui::TreeNodeEx(path.c_str(), flags, "%s", label.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
      _folder = path;
      _filter.clear();
    }
    if (ImGui::BeginDragDropTarget()) {
      // Dropping a file on a folder moves it there.
      if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("JM_ASSET")) {
        const std::string from(static_cast<const char*>(p->Data), static_cast<size_t>(p->DataSize));
        if (from.find('#') == std::string::npos && parentOf(from) != path) editor.moveAsset(from, path + "/" + nameOf(from));
      }
      if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("JM_ENTITY")) {
        _selected = editor.createPrefab(*static_cast<const EntityUid*>(p->Data), path);
        _folder = path;
      }
      ImGui::EndDragDropTarget();
    }
    if (open) {
      drawFolderTree(editor, path, depth + 1);
      ImGui::TreePop();
    }
  }
}

void AssetsPanel::open(Editor& editor, const std::string& path) {
  // A folder's name says nothing about it; the project knows.
  const AssetFile* file = editor.project()->file(path);
  switch (file && file->kind == AssetKind::Folder ? AssetKind::Folder : assetKindOf(path.substr(0, path.find('#')))) {
    case AssetKind::Folder:
      _folder = path;
      _filter.clear();
      break;
    case AssetKind::Scene:
      editor.openScene(path);
      break;
    case AssetKind::Prefab:
      editor.editPrefab(path);
      break;
    case AssetKind::Atlas:
      if (path.find('#') == std::string::npos) editor.openAsset(path);
      break;
    case AssetKind::Image:
    case AssetKind::Sound:
    case AssetKind::Font:
      editor.revealInFileManager(editor.project()->abs(path));
      break;
    default:
      editor.openAsset(path);  // its editor tab, or the code editor
  }
}

void AssetsPanel::contextMenu(Editor& editor, const std::string& path, bool isFolder) {
  Project& project = *editor.project();
  const std::string file = path.substr(0, path.find('#'));
  if (ImGui::MenuItem(ICON_ARROW_SQUARE_OUT "  Open")) open(editor, path);
  if (!isFolder && editor.scene() && !editor.scene()->isPrefab() && ImGui::MenuItem(ICON_PLUS "  Add to Scene")) {
    editor.instantiateAsset(path, editor.scenePanel().viewCenter());
  }
  if (ImGui::MenuItem(ICON_FOLDER_SIMPLE "  Reveal in File Manager")) editor.revealInFileManager(project.abs(file));
  if (ImGui::MenuItem(ICON_COPY "  Copy Path")) ImGui::SetClipboardText(path.c_str());
  if (path.find('#') == std::string::npos) {
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_PENCIL_SIMPLE "  Rename")) {
      _renaming = path;
      _renameText = nameOf(path);
    }
    if (ImGui::MenuItem(ICON_TRASH "  Move to Trash")) editor.deleteAsset(file);
  }
}

void AssetsPanel::draw(Editor& editor) {
  Project& project = *editor.project();
  const bool inAtlas = _folder.ends_with(".atlas.json");
  const float treeWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.2f, 150.0f, 240.0f);

  // Folder tree.
  ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::bg1);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {6, 8});
  ImGui::BeginChild("##tree", {treeWidth, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {4, 3});
  ImGui::PushStyleColor(ImGuiCol_Header, theme::selection);
  if (ImGui::Selectable(ICON_HOUSE "  Project", _folder.empty())) _folder.clear();
  drawFolderTree(editor, "", 0);
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  ImGui::SameLine(0, 0);
  const ImVec2 edge = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddLine(edge, {edge.x, edge.y + ImGui::GetContentRegionAvail().y}, theme::u32(theme::bg0), 2.0f);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10, 8});
  ImGui::BeginChild("##content", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PopStyleVar();

  // Breadcrumbs, search, view options.
  {
    std::vector<std::string> crumbs;
    for (std::string f = _folder; !f.empty(); f = parentOf(f)) crumbs.insert(crumbs.begin(), f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {2, 0});
    ImGui::PushStyleColor(ImGuiCol_Button, theme::withAlpha(theme::bg1, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {6, 4});
    if (ImGui::Button(ICON_HOUSE)) _folder.clear();
    for (const std::string& c : crumbs) {
      ImGui::SameLine();
      ImGui::TextColored(theme::textFaint, ICON_CARET_RIGHT);
      ImGui::SameLine();
      if (ImGui::Button(nameOf(c).c_str())) _folder = c;
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    const float searchWidth = std::min(220.0f, ImGui::GetContentRegionAvail().x * 0.35f);
    ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - searchWidth - 2 * ImGui::GetFrameHeight() - 16);
    if (ui::iconButton("view", _listView ? ICON_SQUARES_FOUR : ICON_LIST,
                       _listView ? "Grid view" : "List view (Ctrl+scroll resizes thumbnails)")) {
      _listView = !_listView;
    }
    ImGui::SameLine(0, 4);
    ImGui::BeginDisabled(inAtlas);
    if (ui::iconButton("new", ICON_PLUS, "New...")) ImGui::OpenPopup("new asset");
    ImGui::EndDisabled();
    ImGui::SameLine(0, 6);
    ui::searchField("search", _filter, "Search all files", searchWidth);
    if (ImGui::BeginPopup("new asset")) {
      const std::string base = _folder.empty() ? std::string("assets") : _folder;
      struct Entry {
        const char* icon;
        const char* label;
        std::string_view kind;  // Editor::newAsset's, or "scene" / "prefab"
      };
      auto section = [&](const char* title, std::initializer_list<Entry> entries) {
        ui::sectionLabel(title, 220);
        for (const Entry& e : entries) {
          if (!ImGui::MenuItem((std::string(e.icon) + "  " + e.label).c_str())) continue;
          if (e.kind == "scene") editor.newScene(_folder);
          else if (e.kind == "prefab") editor.newPrefab(base == "assets" ? std::string("assets/prefabs") : base);
          else editor.newAsset(std::string(e.kind), base);
        }
      };
      section("Content", {{ICON_FILM_SLATE, "Scene", "scene"}, {ICON_CUBE, "Prefab", "prefab"}, {ICON_BROWSER, "UI Screen", "ui"}});
      section("Code", {{ICON_FILE_TS, "Script", "script"}, {ICON_SPARKLE, "Post Effect", "effect"},
                       {ICON_SPARKLE, "Transition", "transition"}, {ICON_PAINT_BRUSH, "Stylesheet", "stylesheet"}});
      section("Data", {{ICON_GRID_FOUR, "Tileset", "tileset"}, {ICON_SQUARES_FOUR, "Atlas", "atlas"},
                       {ICON_TABLE, "Data Table", "data"}, {ICON_GAME_CONTROLLER, "Input Actions", "input"}});
      ImGui::Separator();
      if (ImGui::MenuItem(ICON_FOLDER_PLUS "  Folder")) {
        std::string path = base + "/new_folder";
        for (int n = 2; project.file(path); ++n) path = base + "/new_folder_" + std::to_string(n);
        std::error_code ec;
        fs::create_directories(project.abs(path), ec);
        project.rescan();
        reveal(path);  // where its name field shows
        _renaming = path;
        _renameText = nameOf(path);
      }
      ImGui::EndPopup();
    }
  }
  ImGui::Dummy({0, 4});

  // What to show: the folder's children, an atlas's regions, or search results.
  std::vector<AssetFile> items;
  if (!_filter.empty()) {
    std::vector<std::pair<int, const AssetFile*>> scored;
    for (const AssetFile& f : project.files()) {
      const int score = ui::fuzzyScore(f.path, _filter);
      if (score >= 0 && f.kind != AssetKind::Folder) scored.emplace_back(score, &f);
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& [_, f] : scored) items.push_back(*f);
  } else if (inAtlas) {
    for (const std::string& region : Thumbnails::instance().regions(project, _folder)) {
      items.push_back({_folder + "#" + region, AssetKind::Atlas});
    }
  } else {
    for (const AssetFile& f : project.files()) {
      if (parentOf(f.path) == _folder) items.push_back(f);
    }
    std::stable_partition(items.begin(), items.end(), [](const AssetFile& f) { return f.kind == AssetKind::Folder; });
  }

  ImGui::BeginChild("##items");
  // Ctrl+scroll resizes thumbnails.
  if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0f) {
    _tileSize = std::clamp(_tileSize + ImGui::GetIO().MouseWheel * 8.0f, 56.0f, 160.0f);
    ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, ImGui::GetCurrentWindow()->ID);
  }
  if (items.empty()) {
    if (!_filter.empty()) ui::emptyState(ICON_MAGNIFYING_GLASS, "No files match", "Try fewer letters; search matches anywhere in the path.");
    else if (inAtlas) ui::emptyState(ICON_SQUARES_FOUR, "No regions yet", "Build the project to slice this atlas.");
    else ui::emptyState(ICON_FOLDER_DASHED, "This folder is empty", "Drop files into it from your file manager, or use + to make one.");
  }

  ImDrawList* draw = ImGui::GetWindowDrawList();
  const float tile = _tileSize;
  const float labelHeight = ImGui::GetTextLineHeight() * 2 + 6;
  const int columns = _listView ? 1 : std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + 8) / (tile + 8)));
  int column = 0;
  for (const AssetFile& item : items) {
    ImGui::PushID(item.path.c_str());
    if (column > 0) ImGui::SameLine(0, 8);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size = _listView ? ImVec2(ImGui::GetContentRegionAvail().x, 26.0f) : ImVec2(tile, tile + labelHeight);
    const bool clicked = ImGui::InvisibleButton("##item", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool selected = _selected == item.path;
    const bool isFolder = item.kind == AssetKind::Folder;
    // The name, editable in place (F2, Rename, a new file or folder).
    auto renameField = [&](ImVec2 at, float width) {
      ImGui::SetCursorScreenPos(at);
      ImGui::SetNextItemWidth(width);
      if (ImGui::IsWindowAppearing() || !ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
      if (ImGui::InputText("##rename", &_renameText, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll) ||
          ImGui::IsItemDeactivated()) {
        if (!_renameText.empty() && _renameText != nameOf(item.path) && !ImGui::IsKeyPressed(ImGuiKey_Escape)) {
          const std::string to = (parentOf(item.path).empty() ? "" : parentOf(item.path) + "/") + _renameText;
          if (editor.moveAsset(item.path, to)) _selected = to;
        }
        _renaming.clear();
      }
      // Back to the item, so the next one lines up beside it rather than under the field.
      ImGui::SetCursorScreenPos(pos);
      ImGui::Dummy(size);
    };
    if (clicked) {
      _selected = item.path;
      if (!isFolder) editor.inspectAsset(item.path);
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) open(editor, item.path);
    if (ImGui::BeginPopupContextItem("item menu")) {
      _selected = item.path;
      contextMenu(editor, item.path, isFolder);
      ImGui::EndPopup();
    }
    if (ImGui::BeginDragDropSource()) {
      if (isFolder) {  // into an atlas: all its images
        ImGui::SetDragDropPayload("JM_FOLDER", item.path.data(), item.path.size());
        ImGui::Text(ICON_FOLDER_SIMPLE "  %s", item.path.c_str());
      } else {
        ImGui::SetDragDropPayload("JM_ASSET", item.path.data(), item.path.size());
        if (auto picture = Thumbnails::instance().get(project, item.path)) {
          const float fit = 48.0f / std::max(picture->size.x, picture->size.y);
          ImGui::Image(picture->texture, {picture->size.x * fit, picture->size.y * fit}, picture->uv0, picture->uv1);
          ImGui::SameLine();
        }
        ImGui::TextUnformatted(nameOf(item.path).c_str());
      }
      ImGui::EndDragDropSource();
    }

    const AssetKindInfo info = assetKindInfo(item.kind);
    const ImVec4 color = kindColor(item.kind);
    if (_listView) {
      if (selected || hovered) {
        draw->AddRectFilled(pos, {pos.x + size.x, pos.y + size.y}, theme::u32(selected ? theme::accent : theme::text, selected ? 0.18f : 0.05f),
                            theme::radius);
      }
      draw->AddText({pos.x + 8, pos.y + 5}, theme::u32(color), info.icon);
      if (_renaming == item.path) renameField({pos.x + 28, pos.y + 1}, 260);
      else draw->AddText({pos.x + 32, pos.y + 5}, theme::u32(theme::text), nameOf(item.path).c_str());
      ImGui::PushFont(nullptr, theme::sizeSmall);
      const std::string meta = std::string(info.label) + (item.size ? "   " + sizeLabel(item.size) : "");
      const ImVec2 ms = ImGui::CalcTextSize(meta.c_str());
      draw->AddText({pos.x + size.x - ms.x - 10, pos.y + 7}, theme::u32(theme::textFaint), meta.c_str());
      ImGui::PopFont();
    } else {
      const ImVec2 box{pos.x + tile, pos.y + tile};
      draw->AddRectFilled(pos, box, theme::u32(hovered ? theme::bg3 : theme::bg2), theme::radiusOverlay);
      const auto picture = tilePicture(editor, item);
      if (picture) {
        widgets::fitted(draw, *picture, {pos.x + 10, pos.y + 10}, {box.x - 10, box.y - 10});
      } else {
        ImGui::PushFont(nullptr, tile * 0.34f);
        const ImVec2 is = ImGui::CalcTextSize(info.icon);
        draw->AddText({pos.x + (tile - is.x) * 0.5f, pos.y + (tile - is.y) * 0.5f}, theme::u32(color, 0.9f), info.icon);
        ImGui::PopFont();
      }
      if (selected) draw->AddRect(pos, box, theme::u32(theme::accent), theme::radiusOverlay, 2.0f);
      if (picture && item.kind != AssetKind::Image && item.kind != AssetKind::Atlas) {
        // A pictured prefab, tileset or screen still reads as one: its kind's badge in the corner.
        const ImVec2 b{pos.x + tile - 22, pos.y + 4};
        draw->AddRectFilled(b, {b.x + 18, b.y + 18}, theme::u32(color, 0.9f), theme::radius);
        ImGui::PushFont(nullptr, 12.0f);
        const ImVec2 cs = ImGui::CalcTextSize(info.icon);
        draw->AddText({b.x + (18 - cs.x) * 0.5f, b.y + (18 - cs.y) * 0.5f}, theme::u32(theme::bg0), info.icon);
        ImGui::PopFont();
      }
      // Kind marker in the corner (not for folders and plain pictures).
      if (!isFolder && !picture) {
        draw->AddRectFilled({pos.x + 6, pos.y + tile - 9}, {pos.x + 22, pos.y + tile - 6}, theme::u32(color, 0.9f), 2.0f);
      }

      // Name: up to two lines, centered, ellipsis past that.
      if (_renaming == item.path) {
        renameField({pos.x, box.y + 4}, tile);
      } else {
        const std::string name = nameOf(item.path);
        const float wrap = tile - 4;
        const size_t cut = ImGui::GetFont()->CalcWordWrapPosition(ImGui::GetFontSize(), name.data(), name.data() + name.size(), wrap) - name.data();
        const size_t rest = name.find_first_not_of(' ', cut);
        const std::string lines[] = {name.substr(0, cut), rest == std::string::npos ? "" : ui::ellipsize(name.substr(rest), wrap)};
        for (int i = 0; i < 2; ++i) {
          const float w = ImGui::CalcTextSize(lines[i].c_str()).x;
          draw->AddText({pos.x + (tile - w) * 0.5f, box.y + 4 + i * ImGui::GetTextLineHeight()},
                        theme::u32(selected ? theme::text : theme::textDim), lines[i].c_str());
        }
      }
    }
    if (hovered && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
      ImGui::SetItemTooltip("%s", item.path.c_str());
    }
    ImGui::PopID();
    column = (column + 1) % columns;
  }
  // Clicking the background clears the selection.
  ImGui::Dummy(ImGui::GetContentRegionAvail());
  if (ImGui::IsItemClicked()) _selected.clear();
  // Dropping an entity anywhere on the grid makes it a prefab in this folder.
  if (!inAtlas && ImGui::GetDragDropPayload() && ImGui::GetDragDropPayload()->IsDataType("JM_ENTITY")) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (ImGui::BeginDragDropTargetCustom(window->InnerRect, window->ID)) {
      ImDrawList* fg = ImGui::GetForegroundDrawList();
      fg->AddRect(window->InnerRect.Min, window->InnerRect.Max, theme::u32(theme::accent), theme::radiusOverlay, 2.0f);
      const char* hint = ICON_CUBE "  Drop to make a prefab here";
      const ImVec2 hs = ImGui::CalcTextSize(hint);
      const ImVec2 c = window->InnerRect.GetCenter();
      fg->AddRectFilled({c.x - hs.x * 0.5f - 12, c.y - hs.y * 0.5f - 8}, {c.x + hs.x * 0.5f + 12, c.y + hs.y * 0.5f + 8},
                        theme::u32(theme::bg0, 0.9f), theme::radiusOverlay);
      fg->AddText({c.x - hs.x * 0.5f, c.y - hs.y * 0.5f}, theme::u32(theme::accentBright), hint);
      if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("JM_ENTITY", ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
        _selected = editor.createPrefab(*static_cast<const EntityUid*>(p->Data), _folder);
      }
      ImGui::EndDragDropTarget();
    }
  }
  ImGui::EndChild();
  ImGui::EndChild();
}
