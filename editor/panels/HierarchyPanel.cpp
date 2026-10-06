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

// An eye ending at `right`, toggling hiding in the Scene view: shown on hover
// or while hidden. True while the mouse is over it; `right` moves past it.
bool eye(ImDrawList* draw, float& right, float y, bool rowHovered, bool hidden, const char* tip) {
  const char* icon = hidden ? ICON_EYE_SLASH : ICON_EYE;
  const float x = right - ImGui::CalcTextSize(icon).x;
  const bool over = rowHovered && ImGui::GetMousePos().x >= x - 4 && ImGui::GetMousePos().x <= right + 4;
  if (hidden || rowHovered) draw->AddText({x, y}, theme::u32(over ? theme::text : theme::textFaint), icon);
  if (over) ImGui::SetTooltip("%s", tip);
  right = x - 8;
  return over;
}

}  // namespace

void HierarchyPanel::drawGroupHeader(Editor& editor, const std::string& group,
                                     std::optional<std::pair<EntityUid, std::string>>& regroup) {
  SceneDocument& scene = *editor.scene();
  ImGuiStorage* storage = ImGui::GetStateStorage();
  const ImGuiID openId = ImGui::GetID(("group:" + group).c_str());
  const bool open = storage->GetBool(openId, true);
  std::vector<EntityUid> members;
  for (size_t i = 0; i < scene.size(); ++i) {
    if (scene.entity(i).value("group", std::string()) == group) members.push_back(scene.uid(i));
  }
  const bool hidden = std::all_of(members.begin(), members.end(), [&](EntityUid u) { return editor.hiddenInView(u); });

  ImGui::PushID(("group:" + group).c_str());
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;
  ImGui::SetNextItemAllowOverlap();
  const bool clicked = ImGui::InvisibleButton("##header", {width, kRowHeight});
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  if (hovered) draw->AddRectFilled(pos, {pos.x + width, pos.y + kRowHeight}, theme::u32(theme::text, 0.05f), theme::radius);
  const float ty = pos.y + (kRowHeight - ImGui::GetTextLineHeight()) * 0.5f;
  ImGui::PushFont(nullptr, theme::sizeSmall);
  draw->AddText({pos.x + 6, ty + 1}, theme::u32(theme::textFaint), open ? ICON_CARET_DOWN : ICON_CARET_RIGHT);
  ImGui::PopFont();
  draw->AddText({pos.x + 22, ty}, theme::u32(theme::accent), open ? ICON_FOLDER_OPEN : ICON_FOLDER_SIMPLE);
  ImGui::PushFont(theme::fonts().semibold, 0.0f);
  draw->AddText({pos.x + 44, ty}, theme::u32(theme::text), group.c_str());
  const float nameWidth = ImGui::CalcTextSize(group.c_str()).x;
  ImGui::PopFont();
  ImGui::PushFont(nullptr, theme::sizeSmall);
  draw->AddText({pos.x + 52 + nameWidth, ty + 1}, theme::u32(theme::textFaint), std::to_string(members.size()).c_str());
  ImGui::PopFont();

  float right = pos.x + width - 8;
  const bool overEye = eye(draw, right, ty, hovered, hidden, hidden ? "Show the group in the Scene view" : "Hide the group in the Scene view");
  if (clicked && overEye) {
    for (EntityUid u : members) {
      if (editor.hiddenInView(u) != !hidden) editor.toggleHiddenInView(u);
    }
  } else if (clicked) {
    storage->SetBool(openId, !open);
  }
  if (hovered && !overEye) ImGui::SetItemTooltip("Spawned by a script: Scene.spawnGroup(\"%s\")", group.c_str());

  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("JM_ENTITY")) regroup = std::pair{*static_cast<const EntityUid*>(p->Data), group};
    ImGui::EndDragDropTarget();
  }
  if (ImGui::BeginPopupContextItem("group menu")) {
    if (ImGui::MenuItem(ICON_SELECTION_ALL "  Select Group")) editor.selectAll(members);
    if (ImGui::MenuItem(ICON_PENCIL_SIMPLE "  Rename Group...")) {
      editor.prompt("Rename Group", "Scripts spawning it by its old name need the new one", group, [&editor, group](const std::string& name) {
        editor.scene()->edit("Rename group " + group, [&](Json& doc) {
          for (auto& e : doc["entities"]) {
            if (e.value("group", std::string()) == group) e["group"] = name;
          }
        });
      });
    }
    if (ImGui::MenuItem(ICON_STACK "  Ungroup")) {
      scene.editEntities(members, "Ungroup " + group, [](Json& e) { e.erase("group"); });
    }
    ImGui::EndPopup();
  }
  ImGui::PopID();
}

void HierarchyPanel::draw(Editor& editor) {
  if (editor.playing()) {
    // Scene (the file) or Running (what the game has spawned).
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {2, 0});
    for (int i = 0; i < 2; ++i) {
      const bool active = (i == 1) == editor.showLive();
      if (i) ImGui::SameLine();
      ImGui::PushStyleColor(ImGuiCol_Button, active ? theme::withAlpha(theme::accent, 0.22f) : theme::withAlpha(theme::text, 0.05f));
      ImGui::PushStyleColor(ImGuiCol_Text, active ? theme::accentBright : theme::textDim);
      const float w = (ImGui::GetContentRegionAvail().x - (i ? 0.0f : 2.0f)) / (i ? 1.0f : 2.0f);
      if (ImGui::Button(i ? ICON_PLAY_CIRCLE "  Running" : ICON_FILM_SLATE "  Scene", {w, 0})) editor.showLive() = i == 1;
      ImGui::PopStyleColor(2);
    }
    ImGui::PopStyleVar();
    ImGui::Dummy({0, 2});
    if (editor.showLive()) {
      ui::searchField("filter", _filter, "Search running entities");
      ImGui::Dummy({0, 2});
      drawLive(editor);
      return;
    }
  }
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
  bool create = ui::iconButton("add", ICON_PLUS, "Create entity");
  ImGui::EndDisabled();
  ImGui::Dummy({0, 2});

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
  ImGui::BeginChild("##list", {0, -ImGui::GetTextLineHeightWithSpacing() - 6}, ImGuiChildFlags_None);
  ImGui::PopStyleVar();

  if (scene->size() == 0) {
    create |= ui::emptyState(ICON_CUBE_TRANSPARENT, "This scene is empty", "Create an entity, or drag assets in from the Assets panel.",
                             "Create Entity");
  }

  ImDrawList* draw = ImGui::GetWindowDrawList();
  std::optional<std::pair<EntityUid, int>> move;  // dragged uid, destination index
  std::optional<std::pair<EntityUid, std::string>> regroup;  // dragged uid, new group ("" = none)
  std::string prefabToEdit;  // after the list: editing one replaces the scene under it

  // Ungrouped entities first, then each group (scene order) under a header.
  struct Row {
    std::string header;  // a group's header row when non-empty
    size_t index = 0;
  };
  std::vector<Row> rows;
  std::vector<EntityUid> listed;  // the entity rows' uids, in list order
  auto list = [&](size_t i) {
    if (!_filter.empty() && ui::fuzzyScore(scene->displayName(i), _filter) < 0) return;
    rows.push_back({"", i});
    listed.push_back(scene->uid(i));
  };
  std::vector<std::string> groups;
  for (size_t i = 0; i < scene->size(); ++i) {
    const std::string group = scene->entity(i).value("group", std::string());
    if (group.empty()) list(i);
    else if (std::find(groups.begin(), groups.end(), group) == groups.end()) groups.push_back(group);
  }
  ImGuiStorage* storage = ImGui::GetStateStorage();
  for (const std::string& group : groups) {
    rows.push_back({group, 0});
    if (!storage->GetBool(ImGui::GetID(("group:" + group).c_str()), true) && _filter.empty()) continue;
    for (size_t i = 0; i < scene->size(); ++i) {
      if (scene->entity(i).value("group", std::string()) == group) list(i);
    }
  }

  bool indented = false;
  for (const Row& row : rows) {
    if (!row.header.empty()) {
      if (indented) ImGui::Unindent(14);
      drawGroupHeader(editor, row.header, regroup);
      ImGui::Indent(14);
      indented = true;
      continue;
    }
    const size_t i = row.index;
    if (i >= scene->size()) continue;  // a row's menu deleted entities
    const Json entity = scene->entity(i);  // a copy: a row's menu may edit the scene
    const EntityUid uid = scene->uid(i);
    const std::string name = scene->displayName(i);
    const std::string group = entity.value("group", std::string());
    const Json components = effectiveComponents(project, entity);

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
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
      editor.select(uid);
      editor.scenePanel().frameSelection(editor);
    } else if (clicked) {
      ImGuiIO& io = ImGui::GetIO();
      const auto anchor = std::find(listed.begin(), listed.end(), _lastClicked);
      if (io.KeyShift && _lastClicked && anchor != listed.end()) {
        // Range from the anchor to here, in list order; this row primary.
        const auto here = std::find(listed.begin(), listed.end(), uid);
        std::vector<EntityUid> range{uid};
        std::copy_if(std::min(anchor, here), std::max(anchor, here) + 1, std::back_inserter(range), [&](EntityUid u) { return u != uid; });
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
      ImGui::SetTooltip("%s %s", entityIcon(components), name.c_str());
      ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
      const bool below = ImGui::GetMousePos().y > pos.y + kRowHeight * 0.5f;
      if (const ImGuiPayload* asset = ImGui::AcceptDragDropPayload("JM_ASSET", ImGuiDragDropFlags_AcceptBeforeDelivery |
                                                                                    ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
        const std::string path(static_cast<const char*>(asset->Data), static_cast<size_t>(asset->DataSize));
        if (editor.applyAssetToEntity(uid, path, true)) {
          draw->AddRect(pos, {pos.x + width, pos.y + kRowHeight}, theme::u32(theme::accent), theme::radius, 1.5f);
          if (asset->IsDelivery()) editor.applyAssetToEntity(uid, path);
        }
      }
      if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("JM_ENTITY", ImGuiDragDropFlags_AcceptBeforeDelivery |
                                                                                ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
        const float y = below ? pos.y + kRowHeight : pos.y;
        draw->AddLine({pos.x + 4, y}, {pos.x + width - 4, y}, theme::u32(theme::accent), 2.0f);
        if (p->IsDelivery()) {
          const EntityUid dragged = *static_cast<const EntityUid*>(p->Data);
          move = std::pair{dragged, static_cast<int>(i) + (below ? 1 : 0)};
          regroup = std::pair{dragged, group};  // joins the row's group
        }
      }
      ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem("row menu")) {
      if (!editor.isSelected(uid)) editor.select(uid);
      for (const char* id : {"view.frame", "edit.rename", "edit.duplicate", "edit.copy", "edit.paste"}) editor.commands().menuItem(id);
      if (!isPrefab && !scene->isPrefab()) {
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_CUBE "  Make Prefab")) editor.createPrefab(uid, editor.assetsFolderForPrefabs());
        ui::tooltip("Or drag it into the Assets panel");
      }
      if (!scene->isPrefab() && ImGui::BeginMenu(ICON_STACK "  Move to Group")) {
        if (ImGui::MenuItem("None", nullptr, group.empty())) regroup = std::pair{uid, std::string()};
        for (const std::string& g : groups) {
          if (ImGui::MenuItem(g.c_str(), nullptr, group == g)) regroup = std::pair{uid, g};
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_PLUS "  New Group...")) {
          editor.prompt("New Group", "Group name (scripts spawn it with Scene.spawnGroup)", "room", [&editor](const std::string& g) {
            editor.scene()->editEntities(editor.selection(), "Move to group " + g, [&](Json& e) { e["group"] = g; });
          });
        }
        ImGui::EndMenu();
      }
      if (isPrefab) {
        ImGui::Separator();
        const std::string prefabPath = entity.value("prefab", std::string());
        const bool overridden = !overridesOf(entity).empty();
        if (ImGui::MenuItem(ICON_PENCIL_SIMPLE "  Edit Prefab")) prefabToEdit = prefabPath;
        if (ImGui::MenuItem(ICON_UPLOAD_SIMPLE "  Apply Overrides to Prefab", nullptr, false, overridden)) editor.applyOverrides(uid);
        if (ImGui::MenuItem(ICON_ARROW_U_UP_LEFT "  Revert to Prefab", nullptr, false, overridden)) editor.revertOverrides(uid);
        if (ImGui::MenuItem(ICON_SELECTION_ALL "  Select All Instances")) editor.selectAll(editor.instancesOf(prefabPath));
        if (ImGui::MenuItem(ICON_LINK_BREAK "  Unpack Prefab")) {
          scene->editEntity(uid, "Unpack " + name, [&](Json& e) {
            e.erase("prefab");
            e.erase("overrides");
            e["components"] = components;
          });
        }
        if (ImGui::MenuItem(ICON_FOLDER_SIMPLE "  Show Prefab Asset")) editor.revealAsset(prefabPath);
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
    const bool hidden = editor.hiddenInView(uid);
    if (eye(draw, right, ty, hovered, hidden, hidden ? "Show in the Scene view" : "Hide in the Scene view (the game still has it)") && clicked) {
      editor.toggleHiddenInView(uid);
    }
    if (editor.preview().failed(uid)) marker(ICON_WARNING, theme::warning, "This entity couldn't be built; see the Console.");
    if (isPrefab && !prefabJson(project, entity.value("prefab", std::string()))) {
      marker(ICON_LINK_BREAK, theme::error, "Its prefab file is missing or unreadable.");
    }
    if (components.contains("ScriptComponent")) marker(ICON_CODE, theme::textFaint, "Has a script");
    ImGui::SetCursorScreenPos({pos.x, pos.y + kRowHeight});
    ImGui::PopID();
  }
  if (indented) ImGui::Unindent(14);
  if (listed.empty() && scene->size() > 0) {
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
    // An entity dropped below the list leaves its group.
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("JM_ENTITY")) {
      regroup = std::pair{*static_cast<const EntityUid*>(p->Data), std::string()};
    }
    ImGui::EndDragDropTarget();
  }
  if (ImGui::IsItemClicked()) editor.clearSelection();
  if (ImGui::BeginPopupContextItem("empty menu")) {
    createMenu(editor);
    ImGui::EndPopup();
  }
  ImGui::EndChild();
  if (create) ImGui::OpenPopup("create");
  if (ImGui::BeginPopup("create")) {
    createMenu(editor);
    ImGui::EndPopup();
  }

  // Arrow keys walk the list when the panel has focus; Enter renames.
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && !listed.empty()) {
    const auto at = std::find(listed.begin(), listed.end(), editor.primary());
    const int current = at == listed.end() ? -1 : static_cast<int>(at - listed.begin());
    int next = current;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) next = std::min(static_cast<int>(listed.size()) - 1, current + 1);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) next = std::max(0, current - 1);
    if (next != current) {
      editor.select(listed[static_cast<size_t>(next)]);
      _lastClicked = editor.primary();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) && editor.primary() && !scene->isPrefab()) rename(editor.primary());
  }

  if (regroup) {
    // Moves the dragged entity (and the rest of the selection with it) into the group.
    auto [uid, group] = *regroup;
    std::vector<EntityUid> targets = editor.isSelected(uid) ? editor.selection() : std::vector<EntityUid>{uid};
    const bool changes = std::any_of(targets.begin(), targets.end(), [&](EntityUid t) {
      const Json* e = scene->find(t);
      return e && e->value("group", std::string()) != group;
    });
    if (changes) {
      scene->editEntities(targets, group.empty() ? "Remove from group" : "Move to group " + group, [&](Json& e) {
        if (group.empty()) e.erase("group");
        else e["group"] = group;
      });
    }
  }
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
  if (!prefabToEdit.empty()) editor.editPrefab(prefabToEdit);
}
