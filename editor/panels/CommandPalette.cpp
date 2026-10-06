// The command palette: one search over commands, scenes, files and the
// scene's entities. Prefixes narrow it: ">" commands, "@" entities,
// "scene " scenes.

#include <imgui.h>
#include <imgui_stdlib.h>

#include "Entities.hpp"
#include "Icons.hpp"
#include "Panels.hpp"
#include "Theme.hpp"
#include "Ui.hpp"

namespace {

// Results show grouped, in this order, at most a few per group so one kind
// can't crowd out the rest.
enum class Group { Recent, Commands, Scenes, Entities, Files };
constexpr const char* kGroupNames[] = {"Recent", "Commands", "Scenes", "Entities", "Files"};

struct Result {
  int score;
  std::string label;
  std::string detail;  // shortcut or path, right-aligned
  const char* icon;
  Group group;
  std::function<void()> run;
};

}  // namespace

void CommandPalette::open(const std::string& prefix) {
  _open = true;
  _justOpened = true;
  _query = prefix;
  _cursor = 0;
}

void CommandPalette::draw(Editor& editor) {
  if (!_open) return;
  if (_justOpened) ImGui::OpenPopup("##palette");

  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float width = std::min(620.0f, vp->WorkSize.x - 80.0f);
  ImGui::SetNextWindowPos({vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 70.0f}, ImGuiCond_Always, {0.5f, 0.0f});
  ImGui::SetNextWindowSize({width, 0});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10, 10});
  ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, theme::radiusOverlay + 2);
  ImGui::PushStyleColor(ImGuiCol_PopupBg, theme::bg2);
  ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, theme::withAlpha(theme::bg0, 0.35f));
  if (!ImGui::BeginPopupModal("##palette", nullptr,
                              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    _open = false;
    return;
  }

  // Search box.
  ImGui::PushFont(nullptr, 16.0f);
  if (_justOpened) {
    ImGui::SetKeyboardFocusHere();
    _justOpened = false;
  }
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {12, 9});
  ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::bg1);
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::InputTextWithHint("##query", ICON_MAGNIFYING_GLASS "  Type a command, scene, file, or @entity", &_query)) _cursor = 0;
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
  ImGui::PopFont();

  // Gather results.
  std::string query = _query;
  enum class Mode { All, Commands, Entities, Scenes } mode = Mode::All;
  if (query.starts_with(">")) mode = Mode::Commands, query = query.substr(1);
  else if (query.starts_with("@")) mode = Mode::Entities, query = query.substr(1);
  else if (query.starts_with("scene ")) mode = Mode::Scenes, query = query.substr(6);
  while (!query.empty() && query.front() == ' ') query.erase(query.begin());

  // Scores only rank within a group; an empty query matches everything equally.
  const auto match = [&](std::string_view text) { return query.empty() ? 0 : ui::fuzzyScore(text, query); };
  std::vector<Result> results;
  Commands& commands = editor.commands();
  const auto& recent = commands.recent();
  if (mode == Mode::All || mode == Mode::Commands) {
    for (const Command& c : commands.all()) {
      if (!commands.enabled(c) || c.id == "edit.deleteBack" || c.id == "view.palette2" || c.id == "view.palette") continue;
      const int score = match(c.label);
      if (score < 0) continue;
      // Recently used commands float up when the query is empty.
      const auto it = std::find(recent.begin(), recent.end(), c.id);
      const bool isRecent = it != recent.end();
      if (query.empty() && mode == Mode::All && !isRecent) continue;
      results.push_back({score + (isRecent ? 40 - static_cast<int>(it - recent.begin()) : 0), c.label,
                         c.shortcut ? shortcutLabel(c.shortcut) : c.category, c.icon ? c.icon : ICON_COMMAND,
                         query.empty() && mode == Mode::All ? Group::Recent : Group::Commands, [&commands, id = c.id]() { commands.run(id); }});
    }
  }
  if (Project* project = editor.project()) {
    if (mode == Mode::All || mode == Mode::Scenes) {
      for (const std::string& scene : project->scenes()) {
        const int score = match(scene);
        if (score < 0) continue;
        results.push_back({score, std::filesystem::path(scene).filename().string(), scene, ICON_FILM_SLATE, Group::Scenes,
                           [&editor, scene]() { editor.openScene(scene); }});
      }
    }
    if (mode == Mode::All && !query.empty()) {
      for (const AssetFile& f : project->files()) {
        if (f.kind == AssetKind::Folder || f.kind == AssetKind::Scene) continue;
        const int score = match(f.path);
        if (score < 0) continue;
        results.push_back({score, std::filesystem::path(f.path).filename().string(), f.path, assetKindInfo(f.kind).icon, Group::Files,
                           [&editor, path = f.path, kind = f.kind]() {
                             // Open it where it's edited; otherwise show it.
                             if (kind == AssetKind::Prefab) {
                               editor.editPrefab(path);
                             } else if (Editor::hasAssetEditor(path)) {
                               editor.openAsset(path);
                             } else {
                               editor.revealAsset(path);
                               editor.inspectAsset(path);
                             }
                           }});
      }
    }
  }
  if (SceneDocument* scene = editor.scene(); scene && (mode == Mode::Entities || (mode == Mode::All && !query.empty()))) {
    for (size_t i = 0; i < scene->size(); ++i) {
      const std::string name = scene->displayName(i);
      const int score = match(name);
      if (score < 0) continue;
      const EntityUid uid = scene->uid(i);
      results.push_back({score, name, scene->title(), entityIcon(effectiveComponents(*editor.project(), scene->entity(i))),
                         Group::Entities, [&editor, uid]() {
                           editor.select(uid);
                           editor.scenePanel().frameSelection(editor);
                         }});
    }
  }
  std::stable_sort(results.begin(), results.end(),
                   [](const Result& a, const Result& b) { return a.group != b.group ? a.group < b.group : a.score > b.score; });
  int perGroup[std::size(kGroupNames)] = {};
  std::erase_if(results, [&](const Result& r) { return ++perGroup[static_cast<int>(r.group)] > (r.group == Group::Commands ? 12 : 8); });

  if (results.empty()) {
    ImGui::Dummy({0, 6});
    ui::dimText(query.empty() ? "  Start typing to search." : "  Nothing matches.");
    ImGui::Dummy({0, 6});
  }

  // Keyboard navigation.
  const int count = static_cast<int>(results.size());
  if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) ++_cursor;
  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) --_cursor;
  _cursor = std::clamp(_cursor, 0, std::max(0, count - 1));  // results can shrink under it
  bool runIt = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
  bool close = ImGui::IsKeyPressed(ImGuiKey_Escape);

  // Results.
  const float rowH = 30.0f;
  const float listH = std::min(count * rowH + 30.0f * 3, 420.0f);
  if (count > 0) {
    ImGui::Dummy({0, 2});
    ImGui::BeginChild("##results", {0, listH}, ImGuiChildFlags_None);
    for (int i = 0; i < count; ++i) {
      const Result& r = results[static_cast<size_t>(i)];
      if (i == 0 || r.group != results[static_cast<size_t>(i) - 1].group) {
        ImGui::Dummy({0, 2});
        ui::sectionLabel(kGroupNames[static_cast<int>(r.group)]);
      }
      ImGui::PushID(i);
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float w = ImGui::GetContentRegionAvail().x;
      if (ImGui::InvisibleButton("##r", {w, rowH})) {
        _cursor = i;
        runIt = true;
      }
      if (ImGui::IsItemHovered() && ImGui::GetIO().MouseDelta.x != 0) _cursor = i;
      ImDrawList* draw = ImGui::GetWindowDrawList();
      const bool current = i == _cursor;
      if (current) {
        draw->AddRectFilled(p, {p.x + w, p.y + rowH}, theme::u32(theme::selection), theme::radius);
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)) ImGui::SetScrollHereY();
      }
      const float ty = p.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
      draw->AddText({p.x + 10, ty}, theme::u32(current ? theme::text : theme::textDim), r.icon);
      ImGui::SetCursorScreenPos({p.x + 36, ty});
      ui::fuzzyText(r.label, query, theme::u32(theme::text), theme::u32(theme::accentBright));
      ImGui::PushFont(nullptr, theme::sizeSmall);
      const std::string detail = r.detail.size() > 48 ? "..." + r.detail.substr(r.detail.size() - 45) : r.detail;
      const ImVec2 ds = ImGui::CalcTextSize(detail.c_str());
      draw->AddText({p.x + w - ds.x - 10, p.y + (rowH - ds.y) * 0.5f}, theme::u32(theme::textFaint), detail.c_str());
      ImGui::PopFont();
      ImGui::SetCursorScreenPos({p.x, p.y + rowH});
      ImGui::PopID();
    }
    ImGui::EndChild();
  }

  // Footer hints.
  ImGui::PushFont(nullptr, theme::sizeSmall);
  ImGui::TextColored(theme::textFaint, ICON_ARROWS_DOWN_UP " navigate   " ICON_KEY_RETURN " open   esc close      "
                                       ">  commands   @  entities");
  ImGui::PopFont();

  // Clicking outside closes it.
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) close = true;
  std::function<void()> run;
  if (runIt && _cursor < count) run = std::move(results[static_cast<size_t>(_cursor)].run);
  if (close || run) {
    ImGui::CloseCurrentPopup();
    _open = false;
  }
  ImGui::EndPopup();
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(2);
  if (run) run();  // after the popup is done: it may open another
}
