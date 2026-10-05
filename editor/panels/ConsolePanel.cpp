// The Console: engine log and build output, filterable by level, source and
// text; lines naming a project file open it.

#include <imgui_internal.h>

#include "Icons.hpp"
#include "LogBook.hpp"
#include "Panels.hpp"
#include "Theme.hpp"
#include "Ui.hpp"

namespace {

// A level filter: icon, count, on/off.
bool levelToggle(const char* id, const char* icon, int count, ImVec4 color, bool* on) {
  char label[48];
  std::snprintf(label, sizeof(label), "%s %d##%s", icon, count, id);
  ImGui::PushStyleColor(ImGuiCol_Button, *on ? theme::bg3 : theme::withAlpha(theme::bg3, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_Text, *on ? color : theme::textFaint);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {8, 4});
  const bool pressed = ImGui::Button(label);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(2);
  if (pressed) *on = !*on;
  return pressed;
}

}  // namespace

void ConsolePanel::draw(Editor& editor) {
  LogBook& book = LogBook::instance();
  const auto counts = book.counts();

  // Toolbar.
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10, 6});
  ImGui::BeginChild("##consoleBar", {0, ImGui::GetFrameHeight() + 12}, ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PopStyleVar();
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {4, 0});
  levelToggle("errors", ICON_WARNING_OCTAGON, counts[2], theme::error, &_showErrors);
  ImGui::SameLine();
  levelToggle("warnings", ICON_WARNING, counts[1], theme::warning, &_showWarnings);
  ImGui::SameLine();
  levelToggle("info", ICON_INFO, counts[0], theme::info, &_showInfo);
  ImGui::SameLine(0, 12);
  static const char* kSources[] = {"All", "Game", "Build"};
  for (int i = 0; i < 3; ++i) {
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, _source == i ? theme::bg3 : theme::withAlpha(theme::bg3, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, _source == i ? theme::text : theme::textDim);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {8, 4});
    if (ImGui::Button(kSources[i])) _source = i;
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
  }
  ImGui::PopStyleVar();
  const float searchWidth = std::min(240.0f, ImGui::GetContentRegionAvail().x * 0.4f);
  ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - searchWidth - ImGui::GetFrameHeight() - 6);
  ui::searchField("filter", _filter, "Filter", searchWidth);
  ImGui::SameLine(0, 6);
  if (ui::iconButton("clear", ICON_TRASH, "Clear the console")) book.clear();
  ImGui::EndChild();

  // Lines.
  ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::bg0);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10, 6});
  ImGui::BeginChild("##lines", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PopStyleVar();
  const auto entries = book.snapshot();
  std::vector<const LogBook::Entry*> shown;
  for (const auto& e : entries) {
    if ((e.level == LogBook::Level::Info && !_showInfo) || (e.level == LogBook::Level::Warning && !_showWarnings) ||
        (e.level == LogBook::Level::Error && !_showErrors)) {
      continue;
    }
    if (_source == 1 && e.source == LogBook::Source::Build) continue;
    if (_source == 2 && e.source != LogBook::Source::Build) continue;
    if (!_filter.empty() && ui::fuzzyScore(e.text, _filter) < 0) continue;
    shown.push_back(&e);
  }
  if (shown.empty()) {
    ui::emptyState(ICON_TERMINAL, entries.empty() ? "Nothing logged yet" : "No lines match",
                   entries.empty() ? "Builds, the game's log and errors show up here." : "Change the filters above to see more.");
  }

  ImGui::PushFont(theme::fonts().mono, theme::sizeSmall + 0.5f);
  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(shown.size()));
  while (clipper.Step()) {
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
      const LogBook::Entry& e = *shown[static_cast<size_t>(i)];
      ImGui::PushID(i);
      const ImVec4 color = e.level == LogBook::Level::Error     ? theme::error
                           : e.level == LogBook::Level::Warning ? theme::warning
                           : e.source == LogBook::Source::Build ? theme::text
                                                                : theme::textDim;
      const char* icon = e.level == LogBook::Level::Error ? ICON_WARNING_OCTAGON : e.level == LogBook::Level::Warning ? ICON_WARNING
                         : e.source == LogBook::Source::Build ? ICON_HAMMER : ICON_DOT;
      const ImVec2 pos = ImGui::GetCursorScreenPos();
      const float width = ImGui::GetContentRegionAvail().x;
      const float lineH = ImGui::GetTextLineHeightWithSpacing();
      ImGui::InvisibleButton("##line", {width, lineH});
      const bool hovered = ImGui::IsItemHovered();
      ImDrawList* draw = ImGui::GetWindowDrawList();
      if (hovered) draw->AddRectFilled(pos, {pos.x + width, pos.y + lineH}, theme::u32(theme::text, 0.04f), 3.0f);
      if (e.level == LogBook::Level::Error) draw->AddRectFilled(pos, {pos.x + 2, pos.y + lineH}, theme::u32(theme::error, 0.8f));
      char time[16];
      std::snprintf(time, sizeof(time), "%02d:%02d", static_cast<int>(e.time) / 60, static_cast<int>(e.time) % 60);
      draw->AddText({pos.x + 6, pos.y + 1}, theme::u32(theme::textFaint), time);
      draw->AddText({pos.x + 52, pos.y + 1}, theme::u32(color), icon);
      draw->PushClipRect(pos, {pos.x + width - 40, pos.y + lineH}, true);
      draw->AddText({pos.x + 72, pos.y + 1}, theme::u32(color), e.text.c_str());
      draw->PopClipRect();
      if (e.repeats > 1) {
        char badge[16];
        std::snprintf(badge, sizeof(badge), "%d", e.repeats);
        const ImVec2 bs = ImGui::CalcTextSize(badge);
        const ImVec2 b{pos.x + width - bs.x - 14, pos.y + 1};
        draw->AddRectFilled({b.x - 5, b.y}, {b.x + bs.x + 5, b.y + bs.y}, theme::u32(theme::bg3), bs.y * 0.5f);
        draw->AddText(b, theme::u32(theme::textDim), badge);
      }
      if (hovered && !e.file.empty()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("Open %s%s", e.file.c_str(), e.line ? (":" + std::to_string(e.line)).c_str() : "");
      }
      if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !e.file.empty()) {
        editor.openInCodeEditor(e.file, e.line);
      }
      if (ImGui::BeginPopupContextItem("line menu")) {
        if (ImGui::MenuItem(ICON_COPY "  Copy Line")) ImGui::SetClipboardText(e.text.c_str());
        if (!e.file.empty() && ImGui::MenuItem(ICON_ARROW_SQUARE_OUT "  Open File")) editor.openInCodeEditor(e.file, e.line);
        if (!e.file.empty() && ImGui::MenuItem(ICON_FOLDER_SIMPLE "  Show in Assets")) editor.revealAsset(e.file);
        ImGui::EndPopup();
      }
      ImGui::PopID();
    }
  }
  ImGui::PopFont();

  // Follow new lines unless the user scrolled up to read.
  const uint64_t version = book.version();
  if (version != _seenVersion) {
    if (_stickToBottom) ImGui::SetScrollHereY(1.0f);
    _seenVersion = version;
  }
  _stickToBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f;
  ImGui::EndChild();
  ImGui::PopStyleColor();
}
