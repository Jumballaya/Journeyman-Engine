// The Console: engine log and build output, filterable by level, source and
// text; lines naming a project file open it.

#include "Icons.hpp"
#include "LogBook.hpp"
#include "Panels.hpp"
#include "Theme.hpp"
#include "Ui.hpp"

namespace {

// A flat toggle button, filled when on.
bool chip(const char* label, bool on, ImVec4 onColor, ImVec4 offColor) {
  ImGui::PushStyleColor(ImGuiCol_Button, on ? theme::bg3 : theme::withAlpha(theme::bg3, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_Text, on ? onColor : offColor);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {8, 4});
  const bool pressed = ImGui::Button(label);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(2);
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
  // Errors, warnings, info. A click shows only that level (what you want when you
  // click "3 errors"); clicking it again shows all.
  const char* icons[] = {ICON_INFO, ICON_WARNING, ICON_WARNING_OCTAGON};  // by LogBook::Level
  const ImVec4 colors[] = {theme::info, theme::warning, theme::error};
  for (int level = 2; level >= 0; --level) {
    if (level != 2) ImGui::SameLine();
    char label[48];
    std::snprintf(label, sizeof(label), "%s %d##level%d", icons[level], counts[level], level);
    const bool clicked = chip(label, _showLevel[level], colors[level], theme::textFaint);
    ui::tooltip("Click to see only these; click again for everything.");
    if (!clicked) continue;
    const bool solo = _showLevel[level] && !_showLevel[(level + 1) % 3] && !_showLevel[(level + 2) % 3];
    for (int j = 0; j < 3; ++j) _showLevel[j] = solo || j == level;
  }
  ImGui::SameLine(0, 12);
  static const char* kSources[] = {"All", "Game", "Build"};
  for (int i = 0; i < 3; ++i) {
    ImGui::SameLine();
    if (chip(kSources[i], _source == i, theme::text, theme::textDim)) _source = i;
  }
  ImGui::PopStyleVar();
  const float searchWidth = std::min(240.0f, ImGui::GetContentRegionAvail().x * 0.4f);
  ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - searchWidth - 2 * ImGui::GetFrameHeight() - 8);
  ui::searchField("filter", _filter, "Filter", searchWidth);
  ImGui::SameLine(0, 6);
  if (ui::iconButton("clearOnPlay", ICON_PLAY_CIRCLE, editor.clearConsoleOnPlay() ? "Clears when play starts (on)" : "Clears when play starts (off)",
                     editor.clearConsoleOnPlay())) {
    editor.clearConsoleOnPlay() = !editor.clearConsoleOnPlay();
  }
  ImGui::SameLine(0, 2);
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
    if (!_showLevel[static_cast<int>(e.level)]) continue;
    if (_source && (_source == 2) != (e.source == LogBook::Source::Build)) continue;
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
      const int level = static_cast<int>(e.level);
      const bool build = e.source == LogBook::Source::Build;
      const ImVec4 color = level ? colors[level] : build ? theme::text : theme::textDim;
      const char* icon = level ? icons[level] : build ? ICON_HAMMER : ICON_DOT;
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
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) editor.openInCodeEditor(e.file, e.line);
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
