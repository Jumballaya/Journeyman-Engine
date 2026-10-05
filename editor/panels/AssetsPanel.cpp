// The Assets panel: the project's files as a folder tree and a thumbnail grid.
// Files drag into the scene, the hierarchy and inspector fields; atlases open
// like folders to show their regions.

#include <filesystem>

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include "Icons.hpp"
#include "Panels.hpp"
#include "Theme.hpp"
#include "Thumbnails.hpp"
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

// Top-level code runs once, when the entity spawns; `self` is that entity.
const char* kScriptTemplate = R"(import { Input, Key, Params, self } from "@jm/runtime";

// Runs once, when the entity spawns.
const speed = <f32>Params.number("speed", 100);
const me = self();

// Runs every frame.
export function onUpdate(dt: f32): void {
  const t = me.transform;
  if (Input.keyDown(Key.ArrowRight)) t.x += speed * dt;
  if (Input.keyDown(Key.ArrowLeft)) t.x -= speed * dt;
}
)";

}  // namespace

void AssetsPanel::reveal(const std::string& path) {
  const std::string file = path.substr(0, path.find('#'));
  _folder = parentOf(file);
  _selected = path;
  _filter.clear();
}

void AssetsPanel::drawFolderTree(Editor& editor, const std::string& folder, int depth) {
  const Project& project = *editor.project();
  for (const AssetFile& f : project.files()) {
    if (f.kind != AssetKind::Folder || parentOf(f.path) != folder) continue;
    const bool hasChildren = std::any_of(project.files().begin(), project.files().end(), [&](const AssetFile& c) {
      return c.kind == AssetKind::Folder && parentOf(c.path) == f.path;
    });
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;
    if (_folder == f.path) flags |= ImGuiTreeNodeFlags_Selected;
    if (_folder.starts_with(f.path + "/") || depth == 0) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    const bool current = _folder == f.path;
    const std::string label = std::string(current ? ICON_FOLDER_OPEN : ICON_FOLDER_SIMPLE) + "  " + nameOf(f.path);
    const bool open = ImGui::TreeNodeEx(f.path.c_str(), flags, "%s", label.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
      _folder = f.path;
      _filter.clear();
    }
    if (ImGui::BeginDragDropTarget()) {
      // Dropping a file on a folder moves it there.
      if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("JM_ASSET")) {
        const std::string from(static_cast<const char*>(p->Data), static_cast<size_t>(p->DataSize));
        if (from.find('#') == std::string::npos && parentOf(from) != f.path) {
          editor.moveAsset(from, f.path + "/" + nameOf(from));
        }
      }
      ImGui::EndDragDropTarget();
    }
    if (open) {
      drawFolderTree(editor, f.path, depth + 1);
      ImGui::TreePop();
    }
  }
}

void AssetsPanel::open(Editor& editor, const std::string& path) {
  const AssetKind kind = assetKindOf(path.substr(0, path.find('#')));
  switch (kind) {
    case AssetKind::Folder:
      _folder = path;
      _filter.clear();
      break;
    case AssetKind::Scene:
    case AssetKind::Prefab:
      editor.openScene(path);
      break;
    case AssetKind::Atlas:
      if (path.find('#') == std::string::npos) {
        _folder = path;  // browse into its regions
        _filter.clear();
      }
      break;
    case AssetKind::Image:
    case AssetKind::Sound:
    case AssetKind::Font:
      editor.revealInFileManager(editor.project()->abs(path));
      break;
    default:
      editor.openInCodeEditor(path);
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
  const float treeWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.2f, 150.0f, 240.0f);

  // Folder tree.
  ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::bg1);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {6, 8});
  ImGui::BeginChild("##tree", {treeWidth, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {4, 3});
  ImGui::PushStyleColor(ImGuiCol_Header, theme::withAlpha(theme::accent, 0.18f));
  if (ImGui::Selectable(ICON_HOUSE "  Project", _folder.empty())) _folder.clear();
  drawFolderTree(editor, "", 0);
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  ImGui::SameLine(0, 0);
  ImGui::GetWindowDrawList()->AddLine(ImGui::GetCursorScreenPos(),
                                      {ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + ImGui::GetContentRegionAvail().y},
                                      theme::u32(theme::bg0), 2.0f);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10, 8});
  ImGui::BeginChild("##content", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PopStyleVar();

  // Breadcrumbs, search, view options.
  {
    const bool inAtlas = _folder.ends_with(".atlas.json");
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
      auto unique = [&](const std::string& stem, const std::string& ext) {
        for (int n = 1;; ++n) {
          const std::string p = base + "/" + stem + (n == 1 ? "" : "_" + std::to_string(n)) + ext;
          if (!project.file(p)) return p;
        }
      };
      std::string error;
      if (ImGui::MenuItem(ICON_FOLDER_PLUS "  Folder")) {
        std::error_code ec;
        fs::create_directories(project.abs(unique("new_folder", "")), ec);
        project.rescan();
      }
      if (ImGui::MenuItem(ICON_FILE_TS "  Script")) {
        const std::string path = unique("new_script", ".ts");
        if (project.writeText(path, kScriptTemplate, error)) {
          _selected = path;
          _renaming = path;
          _renameText = nameOf(path);
        }
        project.rescan();
      }
      if (ImGui::MenuItem(ICON_FILM_SLATE "  Scene")) editor.newScene();
      if (ImGui::MenuItem(ICON_BROWSER "  UI Screen")) {
        project.writeText(unique("screen", ".ui.html"), "<div class=\"screen\">\n  <p>New screen</p>\n</div>\n", error);
        project.rescan();
      }
      ImGui::EndPopup();
    }
  }
  ImGui::Dummy({0, 4});

  // What to show: the folder's children, an atlas's regions, or search results.
  struct Item {
    std::string path;
    AssetKind kind;
    uintmax_t size = 0;
  };
  std::vector<Item> items;
  if (!_filter.empty()) {
    std::vector<std::pair<int, const AssetFile*>> scored;
    for (const AssetFile& f : project.files()) {
      const int score = ui::fuzzyScore(f.path, _filter);
      if (score >= 0 && f.kind != AssetKind::Folder) scored.emplace_back(score, &f);
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& [_, f] : scored) items.push_back({f->path, f->kind, f->size});
  } else if (_folder.ends_with(".atlas.json")) {
    for (const std::string& region : Thumbnails::instance().regions(project, _folder)) {
      items.push_back({_folder + "#" + region, AssetKind::Atlas});
    }
  } else {
    for (const AssetFile& f : project.files()) {
      if (parentOf(f.path) == _folder && f.kind == AssetKind::Folder) items.push_back({f.path, f.kind, f.size});
    }
    for (const AssetFile& f : project.files()) {
      if (parentOf(f.path) == _folder && f.kind != AssetKind::Folder) items.push_back({f.path, f.kind, f.size});
    }
  }

  ImGui::BeginChild("##items");
  // Ctrl+scroll resizes thumbnails.
  if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0f) {
    _tileSize = std::clamp(_tileSize + ImGui::GetIO().MouseWheel * 8.0f, 56.0f, 160.0f);
    ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, ImGui::GetCurrentWindow()->ID);
  }
  if (items.empty()) {
    if (!_filter.empty()) ui::emptyState(ICON_MAGNIFYING_GLASS, "No files match", "Try fewer letters; search matches anywhere in the path.");
    else if (_folder.ends_with(".atlas.json")) ui::emptyState(ICON_SQUARES_FOUR, "No regions yet", "Build the project to slice this atlas.");
    else ui::emptyState(ICON_FOLDER_DASHED, "This folder is empty", "Drop files into it from your file manager, or use + to make one.");
  }

  ImDrawList* draw = ImGui::GetWindowDrawList();
  const float tile = _tileSize;
  const float labelHeight = ImGui::GetTextLineHeight() * 2 + 6;
  const int columns = _listView ? 1 : std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + 8) / (tile + 8)));
  int column = 0;
  for (const Item& item : items) {
    ImGui::PushID(item.path.c_str());
    if (column > 0) ImGui::SameLine(0, 8);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size = _listView ? ImVec2(ImGui::GetContentRegionAvail().x, 26.0f) : ImVec2(tile, tile + labelHeight);
    const bool clicked = ImGui::InvisibleButton("##item", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool selected = _selected == item.path;
    if (clicked) {
      _selected = item.path;
      if (item.kind != AssetKind::Folder) editor.inspectAsset(item.path);
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) open(editor, item.path);
    if (ImGui::BeginPopupContextItem("item menu")) {
      _selected = item.path;
      contextMenu(editor, item.path, item.kind == AssetKind::Folder);
      ImGui::EndPopup();
    }
    if (item.kind != AssetKind::Folder && ImGui::BeginDragDropSource()) {
      ImGui::SetDragDropPayload("JM_ASSET", item.path.data(), item.path.size());
      if (auto picture = Thumbnails::instance().get(project, item.path)) {
        const float fit = 48.0f / std::max(picture->size.x, picture->size.y);
        ImGui::Image(picture->texture, {picture->size.x * fit, picture->size.y * fit}, picture->uv0, picture->uv1);
        ImGui::SameLine();
      }
      ImGui::TextUnformatted(nameOf(item.path).c_str());
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
      draw->AddText({pos.x + 32, pos.y + 5}, theme::u32(theme::text), nameOf(item.path).c_str());
      ImGui::PushFont(nullptr, theme::sizeSmall);
      const std::string meta = std::string(info.label) + (item.size ? "   " + sizeLabel(item.size) : "");
      const ImVec2 ms = ImGui::CalcTextSize(meta.c_str());
      draw->AddText({pos.x + size.x - ms.x - 10, pos.y + 7}, theme::u32(theme::textFaint), meta.c_str());
      ImGui::PopFont();
    } else {
      const ImVec2 box{pos.x + tile, pos.y + tile};
      draw->AddRectFilled(pos, box, theme::u32(hovered ? theme::bg3 : theme::bg2), theme::radiusOverlay);
      // Images and regions show themselves; everything else its kind's icon.
      auto picture = (item.kind == AssetKind::Image || item.path.find('#') != std::string::npos)
                         ? Thumbnails::instance().get(project, item.path)
                         : std::nullopt;
      if (item.kind == AssetKind::Atlas && item.path.find('#') == std::string::npos) {
        // An atlas shows its packed sheet.
        const auto regions = Thumbnails::instance().regions(project, item.path);
        if (!regions.empty()) picture = Thumbnails::instance().get(project, item.path + "#" + regions.front());
      }
      if (picture) {
        const float pad = 10.0f;
        const float fit = std::min((tile - pad * 2) / picture->size.x, (tile - pad * 2) / picture->size.y);
        const float scale = fit >= 1.0f ? std::floor(fit) : fit;  // whole-pixel enlargements keep pixel art crisp
        const ImVec2 s{picture->size.x * scale, picture->size.y * scale};
        const ImVec2 a{std::round(pos.x + (tile - s.x) * 0.5f), std::round(pos.y + (tile - s.y) * 0.5f)};
        draw->AddImage(picture->texture, a, {a.x + s.x, a.y + s.y}, picture->uv0, picture->uv1);
      } else {
        ImGui::PushFont(nullptr, tile * 0.34f);
        const ImVec2 is = ImGui::CalcTextSize(info.icon);
        draw->AddText({pos.x + (tile - is.x) * 0.5f, pos.y + (tile - is.y) * 0.5f}, theme::u32(color, 0.9f), info.icon);
        ImGui::PopFont();
      }
      if (selected) draw->AddRect(pos, box, theme::u32(theme::accent), theme::radiusOverlay, 2.0f);
      // Kind marker in the corner (not for folders and plain pictures).
      if (item.kind != AssetKind::Folder && !picture) {
        draw->AddRectFilled({pos.x + 6, pos.y + tile - 9}, {pos.x + 22, pos.y + tile - 6}, theme::u32(color, 0.9f), 2.0f);
      }

      // Name: up to two lines, centered, ellipsis past that.
      if (_renaming == item.path) {
        ImGui::SetCursorScreenPos({pos.x, box.y + 4});
        ImGui::SetNextItemWidth(tile);
        if (ImGui::IsWindowAppearing() || !ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##rename", &_renameText, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll) ||
            ImGui::IsItemDeactivated()) {
          if (!_renameText.empty() && _renameText != nameOf(item.path) && !ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            const std::string to = (parentOf(item.path).empty() ? "" : parentOf(item.path) + "/") + _renameText;
            if (editor.moveAsset(item.path, to)) _selected = to;
          }
          _renaming.clear();
        }
      } else {
        const std::string name = nameOf(item.path);
        const float wrap = tile - 4;
        ImFont* font = ImGui::GetFont();
        const float fs = ImGui::GetFontSize();
        const char* text = name.c_str();
        const char* end = text + name.size();
        float y = box.y + 4;
        for (int line = 0; line < 2 && text < end; ++line) {
          const char* lineEnd = font->CalcWordWrapPosition(fs, text, end, wrap);
          if (lineEnd == text) lineEnd = std::min(end, text + 1);
          std::string piece(text, lineEnd);
          if (line == 1 && lineEnd < end) {
            while (!piece.empty() && ImGui::CalcTextSize((piece + "...").c_str()).x > wrap) piece.pop_back();
            piece += "...";
          }
          const float w = ImGui::CalcTextSize(piece.c_str()).x;
          draw->AddText({pos.x + (tile - w) * 0.5f, y}, theme::u32(selected ? theme::text : theme::textDim), piece.c_str());
          y += ImGui::GetTextLineHeight();
          text = lineEnd;
          while (text < end && *text == ' ') ++text;
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
  ImGui::EndChild();
  ImGui::EndChild();
}
