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

struct Result {
  int score;
  std::string label;
  std::string detail;  // shortcut or path, right-aligned
  const char* icon;
  std::string group;
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

  std::vector<Result> results;
  Commands& commands = editor.commands();
  if (mode == Mode::All || mode == Mode::Commands) {
    for (const Command& c : commands.all()) {
      if (!commands.enabled(c) || c.id == "edit.deleteBack" || c.id == "view.palette2" || c.id == "view.palette") continue;
      const int score = query.empty() ? 0 : ui::fuzzyScore(c.label, query);
      if (score < 0) continue;
      // Recently used commands float up when the query is empty.
      const auto& recent = commands.recent();
      const auto it = std::find(recent.begin(), recent.end(), c.id);
      const bool isRecent = it != recent.end();
      if (query.empty() && mode == Mode::All && !isRecent) continue;
      results.push_back({score + (isRecent ? 40 - static_cast<int>(it - recent.begin()) : 0), c.label,
                         c.shortcut ? shortcutLabel(c.shortcut) : c.category, c.icon ? c.icon : ICON_COMMAND,
                         query.empty() && mode == Mode::All ? "Recent" : "Commands", [&commands, id = c.id]() { commands.run(id); }});
    }
  }
  if (Project* project = editor.project()) {
    if (mode == Mode::All || mode == Mode::Scenes) {
      for (const std::string& scene : project->scenes()) {
        const int score = query.empty() ? 1 : ui::fuzzyScore(scene, query);
        if (score < 0) continue;
        results.push_back({score + 5, std::filesystem::path(scene).filename().string(), scene, ICON_FILM_SLATE, "Scenes",
                           [&editor, scene]() { editor.openScene(scene); }});
      }
    }
    if (mode == Mode::All && !query.empty()) {
      for (const AssetFile& f : project->files()) {
        if (f.kind == AssetKind::Folder || f.kind == AssetKind::Scene) continue;
        const int score = ui::fuzzyScore(f.path, query);
        if (score < 0) continue;
        results.push_back({score, std::filesystem::path(f.path).filename().string(), f.path, assetKindInfo(f.kind).icon, "Files",
                           [&editor, path = f.path]() { editor.revealAsset(path); }});
      }
    }
  }
  if (SceneDocument* scene = editor.scene(); scene && (mode == Mode::Entities || (mode == Mode::All && !query.empty()))) {
    for (size_t i = 0; i < scene->size(); ++i) {
      const std::string name = scene->displayName(i);
      const int score = query.empty() ? 0 : ui::fuzzyScore(name, query);
      if (score < 0) continue;
      const EntityUid uid = scene->uid(i);
      results.push_back({score + 3, name, scene->title(), entityIcon(effectiveComponents(*editor.project(), scene->entity(i))),
                         "Entities", [&editor, uid]() {
                           editor.select(uid);
                           editor.scenePanel().frameSelection(editor);
                         }});
    }
  }
  // Best first, but keep groups together in a stable order.
  static const std::vector<std::string> kGroupOrder = {"Recent", "Commands", "Scenes", "Entities", "Files"};
  std::stable_sort(results.begin(), results.end(), [](const Result& a, const Result& b) {
    const auto ga = std::find(kGroupOrder.begin(), kGroupOrder.end(), a.group) - kGroupOrder.begin();
    const auto gb = std::find(kGroupOrder.begin(), kGroupOrder.end(), b.group) - kGroupOrder.begin();
    return ga != gb ? ga < gb : a.score > b.score;
  });
  // A few per group so one kind can't crowd out the rest.
  {
    std::map<std::string, int> perGroup;
    std::erase_if(results, [&](const Result& r) { return ++perGroup[r.group] > (r.group == "Commands" ? 12 : 8); });
  }

  if (results.empty()) {
    ImGui::Dummy({0, 6});
    ui::dimText(query.empty() ? "  Start typing to search." : "  Nothing matches.");
    ImGui::Dummy({0, 6});
  }

  // Keyboard navigation.
  const int count = static_cast<int>(results.size());
  if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) _cursor = std::min(count - 1, _cursor + 1);
  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) _cursor = std::max(0, _cursor - 1);
  bool runIt = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
  bool close = ImGui::IsKeyPressed(ImGuiKey_Escape);

  // Results.
  const float rowH = 30.0f;
  const float listH = std::min(count * rowH + 30.0f * 3, 420.0f);
  if (count > 0) {
    ImGui::Dummy({0, 2});
    ImGui::BeginChild("##results", {0, listH}, ImGuiChildFlags_None);
    std::string group;
    for (int i = 0; i < count; ++i) {
      const Result& r = results[static_cast<size_t>(i)];
      if (r.group != group) {
        group = r.group;
        ImGui::Dummy({0, 2});
        ui::sectionLabel(group.c_str());
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
        draw->AddRectFilled(p, {p.x + w, p.y + rowH}, theme::u32(theme::accent, 0.18f), theme::radius);
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)) ImGui::SetScrollHereY();
      }
      const float ty = p.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
      draw->AddText({p.x + 10, ty}, theme::u32(current ? theme::accentBright : theme::textDim), r.icon);
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

  if (runIt && _cursor >= 0 && _cursor < count) {
    auto run = results[static_cast<size_t>(_cursor)].run;
    close = true;
    ImGui::CloseCurrentPopup();
    _open = false;
    ImGui::EndPopup();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    run();
    return;
  }
  // Clicking outside closes it.
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) close = true;
  if (close) {
    ImGui::CloseCurrentPopup();
    _open = false;
  }
  ImGui::EndPopup();
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(2);
}
