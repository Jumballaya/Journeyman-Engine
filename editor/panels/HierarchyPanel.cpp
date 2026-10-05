// The Hierarchy: the scene's entities as a list, with search, multi-select,
// inline rename, drag-to-reorder and a context menu.

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include "Entities.hpp"
#include "Icons.hpp"
#include "Panels.hpp"
#include "Theme.hpp"
#include "Ui.hpp"

namespace {

constexpr float kRowHeight = 26.0f;

void createMenu(Editor& editor) {
  for (const auto& [kind, icon] : std::vector<std::pair<const char*, const char*>>{
           {"Empty", ICON_CUBE_TRANSPARENT}, {"Sprite", ICON_IMAGE}, {"Text", ICON_TEXT_T}, {"Tile Map", ICON_GRID_FOUR},
           {"UI Screen", ICON_BROWSER}, {"Sound", ICON_SPEAKER_HIGH}, {"Script", ICON_CODE}}) {
    const std::string label = std::string(icon) + "  " + kind;
    if (ImGui::MenuItem(label.c_str())) editor.createEntity(kind, editor.scenePanel().viewCenter());
  }
}

}  // namespace

void HierarchyPanel::draw(Editor& editor) {
  SceneDocument* scene = editor.scene();
  if (!scene) {
    ui::emptyState(ICON_TREE_STRUCTURE, "No scene", "Open a scene to see its entities.");
    return;
  }
  const Project& project = *editor.project();

  // Search and create.
  const float addWidth = ImGui::GetFrameHeight();
  ui::searchField("filter", _filter, "Search entities", ImGui::GetContentRegionAvail().x - addWidth - 6);
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F)) {
    ImGui::SetKeyboardFocusHere(-1);
  }
  ImGui::SameLine(0, 6);
  ImGui::BeginDisabled(scene->isPrefab());
  if (ui::iconButton("add", ICON_PLUS, "Create entity")) ImGui::OpenPopup("create");
  ImGui::EndDisabled();
  if (ImGui::BeginPopup("create")) {
    createMenu(editor);
    ImGui::EndPopup();
  }
  ImGui::Dummy({0, 2});

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
  ImGui::BeginChild("##list", {0, -ImGui::GetTextLineHeightWithSpacing() - 6}, ImGuiChildFlags_None);
  ImGui::PopStyleVar();

  if (scene->size() == 0) {
    if (ui::emptyState(ICON_CUBE_TRANSPARENT, "This scene is empty", "Create an entity, or drag assets in from the Assets panel.",
                       "Create Entity")) {
      ImGui::OpenPopup("create");
    }
    if (ImGui::BeginPopup("create")) {
      createMenu(editor);
      ImGui::EndPopup();
    }
  }

  ImDrawList* draw = ImGui::GetWindowDrawList();
  int shown = 0;
  std::optional<std::pair<EntityUid, int>> move;  // dragged uid, destination index
  for (size_t i = 0; i < scene->size(); ++i) {
    const Json entity = scene->entity(i);  // a copy: a row's menu may delete entities mid-loop
    const EntityUid uid = scene->uid(i);
    const std::string name = scene->displayName(i);
    if (!_filter.empty() && ui::fuzzyScore(name, _filter) < 0) continue;
    ++shown;

    ImGui::PushID(static_cast<int>(uid));
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const bool selected = editor.isSelected(uid);
    const bool isPrefab = entity.contains("prefab");

    if (_renaming == uid) {
      ImGui::SetCursorScreenPos({pos.x + 28, pos.y + 1});
      ImGui::SetNextItemWidth(width - 34);
      const bool focusing = _renameFocus;
      if (focusing) {
        _renameText = entity.value("name", name);
        ImGui::SetKeyboardFocusHere();
        _renameFocus = false;
      }
      const bool done = ImGui::InputText("##rename", &_renameText, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
      // A box that never got the keyboard (focus went elsewhere) just closes.
      if (!focusing && !ImGui::IsItemActive() && !ImGui::IsItemDeactivated()) _renaming = 0;
      if (done || ImGui::IsItemDeactivated()) {
        if (!ImGui::IsKeyPressed(ImGuiKey_Escape) && !_renameText.empty() && _renameText != entity.value("name", std::string())) {
          scene->editEntity(uid, "Rename to " + _renameText, [&](Json& e) { e["name"] = _renameText; });
        }
        _renaming = 0;
      }
      ImGui::SetCursorScreenPos({pos.x, pos.y + kRowHeight});
      ImGui::PopID();
      continue;
    }

    ImGui::SetNextItemAllowOverlap();
    const bool clicked = ImGui::InvisibleButton("##row", {width, kRowHeight});
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
      editor.select(uid);
      editor.scenePanel().frameSelection(editor);
    } else if (clicked) {
      ImGuiIO& io = ImGui::GetIO();
      if (io.KeyShift && _lastClicked && scene->indexOf(_lastClicked) >= 0) {
        // Range from the anchor to here, in list order.
        const int a = scene->indexOf(_lastClicked), b = static_cast<int>(i);
        std::vector<EntityUid> range;
        for (int k = std::min(a, b); k <= std::max(a, b); ++k) range.push_back(scene->uid(static_cast<size_t>(k)));
        std::erase(range, uid);
        range.insert(range.begin(), uid);
        editor.selectAll(range);
      } else if (io.KeyCtrl) {
        editor.select(uid, Editor::SelectMode::Toggle);
        _lastClicked = uid;
      } else {
        editor.select(uid);
        _lastClicked = uid;
      }
    }

    // Drag to reorder; drop above or below a row.
    if (!scene->isPrefab() && _filter.empty() && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
      ImGui::SetDragDropPayload("JM_ENTITY", &uid, sizeof(uid));
      ImGui::SetTooltip("%s %s", entityIcon(effectiveComponents(project, entity)), name.c_str());
      ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
      const bool below = ImGui::GetMousePos().y > pos.y + kRowHeight * 0.5f;
      if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("JM_ENTITY", ImGuiDragDropFlags_AcceptBeforeDelivery |
                                                                                ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
        const float y = below ? pos.y + kRowHeight : pos.y;
        draw->AddLine({pos.x + 4, y}, {pos.x + width - 4, y}, theme::u32(theme::accent), 2.0f);
        if (p->IsDelivery()) move = std::pair{*static_cast<const EntityUid*>(p->Data), static_cast<int>(i) + (below ? 1 : 0)};
      }
      ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem("row menu")) {
      if (!editor.isSelected(uid)) editor.select(uid);
      for (const char* id : {"view.frame", "edit.rename", "edit.duplicate"}) editor.commands().menuItem(id);
      if (isPrefab) {
        ImGui::Separator();
        const std::string prefabPath = entity.value("prefab", std::string());
        if (ImGui::MenuItem(ICON_CUBE "  Open Prefab")) editor.openScene(prefabPath);
        if (ImGui::MenuItem(ICON_LINK_BREAK "  Unpack Prefab")) {
          const Json components = effectiveComponents(project, entity);
          scene->editEntity(uid, "Unpack " + name, [&](Json& e) {
            e.erase("prefab");
            e.erase("overrides");
            e["components"] = components;
          });
        }
        if (ImGui::MenuItem(ICON_FOLDER_SIMPLE "  Show in Assets")) editor.revealAsset(prefabPath);
      }
      ImGui::Separator();
      editor.commands().menuItem("edit.delete");
      ImGui::EndPopup();
    }

    // Row visuals.
    if (selected) {
      draw->AddRectFilled(pos, {pos.x + width, pos.y + kRowHeight}, theme::u32(theme::accent, uid == editor.primary() ? 0.22f : 0.14f),
                          theme::radius);
    } else if (hovered) {
      draw->AddRectFilled(pos, {pos.x + width, pos.y + kRowHeight}, theme::u32(theme::text, 0.05f), theme::radius);
    }
    const float ty = pos.y + (kRowHeight - ImGui::GetTextLineHeight()) * 0.5f;
    const Json components = effectiveComponents(project, entity);
    const ImVec4 iconColor = isPrefab ? theme::info : selected ? theme::accentBright : theme::textDim;
    draw->AddText({pos.x + 8, ty}, theme::u32(iconColor), isPrefab ? ICON_CUBE : entityIcon(components));
    const ImVec4 textColor = isPrefab ? theme::info : theme::text;
    if (_filter.empty()) {
      draw->AddText({pos.x + 30, ty}, theme::u32(textColor), name.c_str());
    } else {
      ImGui::SetCursorScreenPos({pos.x + 30, ty});
      ui::fuzzyText(name, _filter, theme::u32(textColor), theme::u32(theme::accentBright));
    }
    // Right side: a warning for entries that failed to build, else what makes it tick.
    float right = pos.x + width - 8;
    auto marker = [&](const char* icon, ImVec4 color, const char* tip) {
      const ImVec2 s = ImGui::CalcTextSize(icon);
      right -= s.x;
      draw->AddText({right, ty}, theme::u32(color), icon);
      if (hovered && ImGui::GetMousePos().x >= right - 2 && ImGui::GetMousePos().x <= right + s.x + 2) ImGui::SetTooltip("%s", tip);
      right -= 6;
    };
    if (editor.preview().failed(uid)) marker(ICON_WARNING, theme::warning, "This entity couldn't be built; see the Console.");
    if (components.contains("ScriptComponent")) marker(ICON_CODE, theme::textFaint, "Has a script");
    ImGui::SetCursorScreenPos({pos.x, pos.y + kRowHeight});
    ImGui::PopID();
  }
  if (shown == 0 && scene->size() > 0) {
    ImGui::Dummy({0, 12});
    ui::dimText("  No entities match.");
  }

  // Drops onto the empty area: assets become entities.
  ImGui::Dummy(ImGui::GetContentRegionAvail());
  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("JM_ASSET")) {
      editor.instantiateAsset(std::string(static_cast<const char*>(p->Data), static_cast<size_t>(p->DataSize)),
                              editor.scenePanel().viewCenter());
    }
    ImGui::EndDragDropTarget();
  }
  if (ImGui::IsItemClicked()) editor.clearSelection();
  if (ImGui::BeginPopupContextItem("empty menu")) {
    createMenu(editor);
    ImGui::EndPopup();
  }
  ImGui::EndChild();

  if (move) {
    const auto [uid, to] = *move;
    scene->edit("Reorder", [&](Json& doc) {
      Json& list = doc["entities"];
      for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].value(kUidKey, EntityUid{0}) != uid) continue;
        Json moved = list[i];
        list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
        const size_t at = std::min(static_cast<size_t>(to > static_cast<int>(i) ? to - 1 : to), list.size());
        list.insert(list.begin() + static_cast<std::ptrdiff_t>(at), moved);
        return;
      }
    });
  }

  // Footer: how many, and what's selected.
  ImGui::PushFont(nullptr, theme::sizeSmall);
  ImGui::TextColored(theme::textFaint, "%zu %s%s", scene->size(), scene->size() == 1 ? "entity" : "entities",
                     editor.selection().empty() ? "" : ("  ·  " + std::to_string(editor.selection().size()) + " selected").c_str());
  ImGui::PopFont();
}
