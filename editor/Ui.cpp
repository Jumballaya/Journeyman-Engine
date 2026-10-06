#include "Ui.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include "Commands.hpp"
#include "Icons.hpp"
#include "Theme.hpp"

namespace ui {

void tooltip(const char* text, ImGuiKeyChord shortcut) {
  if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay |
                            ImGuiHoveredFlags_AllowWhenDisabled)) {
    return;
  }
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8, 6});
  ImGui::BeginTooltip();
  ImGui::TextUnformatted(text);
  if (shortcut) {
    ImGui::SameLine(0, 10);
    ImGui::TextColored(theme::textDim, "%s", shortcutLabel(shortcut).c_str());
  }
  ImGui::EndTooltip();
  ImGui::PopStyleVar();
}

bool iconButton(const char* id, const char* icon, const char* tip, bool active, ImGuiKeyChord shortcut, float size) {
  const float side = size > 0.0f ? size : ImGui::GetFrameHeight();
  ImGui::PushID(id);
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const bool pressed = ImGui::InvisibleButton("##b", {side, side});
  const bool hovered = ImGui::IsItemHovered();
  const bool held = ImGui::IsItemActive();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 end{pos.x + side, pos.y + side};
  if (active) {
    draw->AddRectFilled(pos, end, theme::u32(theme::accent, held ? 0.32f : 0.22f), theme::radius);
  } else if (hovered || held) {
    draw->AddRectFilled(pos, end, theme::u32(theme::text, held ? 0.12f : 0.07f), theme::radius);
  }
  const bool disabled = ImGui::GetItemFlags() & ImGuiItemFlags_Disabled;
  const ImVec4 color = disabled ? theme::textFaint : active ? theme::accentBright : hovered ? theme::text : theme::textDim;
  const ImVec2 textSize = ImGui::CalcTextSize(icon);
  draw->AddText({pos.x + (side - textSize.x) * 0.5f, pos.y + (side - textSize.y) * 0.5f}, theme::u32(color), icon);
  ImGui::PopID();
  if (tip) tooltip(tip, shortcut);
  return pressed;
}

namespace {

bool styledButton(const char* label, ImVec2 size, ImVec4 base, ImVec4 hover, ImVec4 active, ImVec4 text) {
  ImGui::PushStyleColor(ImGuiCol_Button, base);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
  ImGui::PushStyleColor(ImGuiCol_Text, text);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {12, 6});
  ImGui::PushFont(theme::fonts().medium, 0.0f);
  const bool pressed = ImGui::Button(label, size);
  ImGui::PopFont();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(4);
  return pressed;
}

}  // namespace

bool primaryButton(const char* label, ImVec2 size) {
  return styledButton(label, size, theme::accent, theme::accentBright, theme::accentDeep, theme::bg0);
}

bool button(const char* label, ImVec2 size) {
  return styledButton(label, size, theme::bg3, theme::bg4, theme::withAlpha(theme::accent, 0.35f), theme::text);
}

bool dangerButton(const char* label, ImVec2 size) {
  return styledButton(label, size, theme::withAlpha(theme::error, 0.85f), theme::error,
                      theme::withAlpha(theme::error, 0.7f), theme::text);
}

bool searchField(const char* id, std::string& text, const char* hint, float width) {
  ImGui::PushID(id);
  const float w = width < 0.0f ? ImGui::GetContentRegionAvail().x : width;
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float h = ImGui::GetFrameHeight();
  const float iconPad = 26.0f;

  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {iconPad, ImGui::GetStyle().FramePadding.y});
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, h * 0.5f);
  ImGui::SetNextItemWidth(w);
  bool changed = ImGui::InputTextWithHint("##search", hint, &text);
  const bool focused = ImGui::IsItemActive();
  ImGui::PopStyleVar(2);

  ImDrawList* draw = ImGui::GetWindowDrawList();
  if (focused) draw->AddRect(pos, {pos.x + w, pos.y + h}, theme::u32(theme::accent, 0.6f), h * 0.5f, 1.0f);
  const ImVec2 glyph = ImGui::CalcTextSize(ICON_MAGNIFYING_GLASS);
  draw->AddText({pos.x + 9.0f, pos.y + (h - glyph.y) * 0.5f}, theme::u32(theme::textFaint), ICON_MAGNIFYING_GLASS);

  if (!text.empty()) {
    const ImVec2 back = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos({pos.x + w - h, pos.y});
    if (iconButton("clear", ICON_X, "Clear", false, 0, h)) {
      text.clear();
      changed = true;
    }
    ImGui::SetCursorScreenPos(back);
  }
  ImGui::PopID();
  return changed;
}

bool toggle(const char* id, bool* value) {
  const float h = ImGui::GetFrameHeight() * 0.72f;
  const float w = h * 1.75f;
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float yOffset = (ImGui::GetFrameHeight() - h) * 0.5f;
  const bool pressed = ImGui::InvisibleButton(id, {w, ImGui::GetFrameHeight()});
  if (pressed) *value = !*value;

  ImGuiStorage* storage = ImGui::GetStateStorage();
  const ImGuiID animId = ImGui::GetItemID();
  float t = storage->GetFloat(animId, *value ? 1.0f : 0.0f);
  t += (*value ? 1.0f : -1.0f) * ImGui::GetIO().DeltaTime * 10.0f;  // ~0.1 s slide
  t = std::clamp(t, 0.0f, 1.0f);
  storage->SetFloat(animId, t);

  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 a{pos.x, pos.y + yOffset}, b{pos.x + w, pos.y + yOffset + h};
  const ImVec4 off = ImGui::IsItemHovered() ? theme::bg4 : theme::bg3;
  const ImVec4 track = ImLerp(off, theme::accent, t);
  draw->AddRectFilled(a, b, theme::u32(track), h * 0.5f);
  const float r = h * 0.5f - 2.5f;
  draw->AddCircleFilled({a.x + h * 0.5f + t * (w - h), a.y + h * 0.5f}, r, theme::u32(theme::text));
  return pressed;
}

bool emptyState(const char* icon, const char* title, const char* body, const char* action) {
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float wrap = std::min(avail.x - 32.0f, 260.0f);
  ImGui::PushFont(nullptr, 30.0f);
  const ImVec2 iconSize = ImGui::CalcTextSize(icon);
  ImGui::PopFont();
  const float bodyHeight = ImGui::CalcTextSize(body, nullptr, false, wrap).y;
  const float total = iconSize.y + 12 + ImGui::GetTextLineHeight() + 6 + bodyHeight + (action ? 40 : 0);
  const ImVec2 start = ImGui::GetCursorPos();
  ImGui::SetCursorPosY(start.y + std::max(0.0f, (avail.y - total) * 0.42f));

  auto centered = [&](float width) { ImGui::SetCursorPosX(start.x + std::max(0.0f, (avail.x - width) * 0.5f)); };
  ImGui::PushFont(nullptr, 30.0f);
  centered(iconSize.x);
  ImGui::TextColored(theme::textFaint, "%s", icon);
  ImGui::PopFont();
  ImGui::Dummy({0, 4});
  ImGui::PushFont(theme::fonts().semibold, 0.0f);
  centered(ImGui::CalcTextSize(title).x);
  ImGui::TextUnformatted(title);
  ImGui::PopFont();

  // Wrapped, each line centered.
  ImGui::PushStyleColor(ImGuiCol_Text, theme::textDim);
  const char* p = body;
  const char* end = body + std::strlen(body);
  ImFont* font = ImGui::GetFont();
  while (p < end) {
    const char* lineEnd = font->CalcWordWrapPosition(ImGui::GetFontSize(), p, end, wrap);
    if (lineEnd == p) lineEnd = end;
    centered(ImGui::CalcTextSize(p, lineEnd).x);
    ImGui::TextUnformatted(p, lineEnd);
    p = lineEnd;
    while (p < end && (*p == ' ' || *p == '\n')) ++p;
  }
  ImGui::PopStyleColor();

  if (!action) return false;
  ImGui::Dummy({0, 6});
  centered(ImGui::CalcTextSize(action).x + 24.0f);
  return button(action);
}

void spinner(float radius, ImU32 color) {
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::Dummy({radius * 2, radius * 2});
  const float t = static_cast<float>(ImGui::GetTime()) * 6.0f;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->PathArcTo({pos.x + radius, pos.y + radius}, radius - 1.5f, t, t + IM_PI * 1.3f, 24);
  draw->PathStroke(color, 2.0f);
}

void heading(const char* text) {
  ImGui::PushFont(theme::fonts().semibold, 0.0f);
  ImGui::TextUnformatted(text);
  ImGui::PopFont();
}

void dimText(const char* text) { ImGui::TextColored(theme::textDim, "%s", text); }

void smallText(const char* text, ImVec4 color) {
  ImGui::PushFont(nullptr, theme::sizeSmall);
  ImGui::PushTextWrapPos(0.0f);  // wraps at the panel's edge
  ImGui::TextColored(color, "%s", text);
  ImGui::PopTextWrapPos();
  ImGui::PopFont();
}

void sectionLabel(const char* text, float width) {
  ImGui::Dummy({0, 2});
  const float right = ImGui::GetCursorScreenPos().x + (width > 0.0f ? width : ImGui::GetContentRegionAvail().x);
  ImGui::PushFont(theme::fonts().medium, theme::sizeSmall);
  ImGui::TextColored(theme::textFaint, "%s", text);
  ImGui::PopFont();
  const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
  if (max.x + 8 < right) {
    ImGui::GetWindowDrawList()->AddLine({max.x + 8, (min.y + max.y) * 0.5f}, {right, (min.y + max.y) * 0.5f},
                                        theme::u32(theme::border));
  }
}

std::string ellipsize(const std::string& text, float width) {
  if (ImGui::CalcTextSize(text.c_str()).x <= width) return text;
  std::string out = text;
  while (!out.empty()) {
    out.pop_back();
    while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) out.pop_back();  // whole UTF-8 characters
    if (ImGui::CalcTextSize((out + "\xE2\x80\xA6").c_str()).x <= width) break;
  }
  return out + "\xE2\x80\xA6";
}

std::string displayPath(const std::string& path) {
  const char* home = std::getenv("HOME");
  if (home && *home && path.starts_with(home)) return "~" + path.substr(std::strlen(home));
  return path;
}

bool beginProperties(const char* id, float labelWidth) {
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {4, 3});
  if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
    ImGui::PopStyleVar();
    return false;
  }
  const float width = labelWidth > 0.0f ? labelWidth : std::clamp(ImGui::GetContentRegionAvail().x * 0.36f, 80.0f, 150.0f);
  ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, width);
  ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
  return true;
}

void propertyRow(const char* label, const char* hint, bool modified) {
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::AlignTextToFramePadding();
  const ImVec2 rowStart = ImGui::GetCursorScreenPos();
  if (modified) {
    ImGui::GetWindowDrawList()->AddRectFilled({rowStart.x - 6, rowStart.y + 2}, {rowStart.x - 4, rowStart.y + ImGui::GetFrameHeight() - 2},
                                              theme::u32(theme::accent));
  }
  ImGui::PushStyleColor(ImGuiCol_Text, modified ? theme::text : theme::textDim);
  ImGui::TextUnformatted(label);
  ImGui::PopStyleColor();
  if (hint && *hint) tooltip(hint);
  ImGui::TableSetColumnIndex(1);
  ImGui::SetNextItemWidth(-FLT_MIN);
}

void endProperties() {
  ImGui::EndTable();
  ImGui::PopStyleVar();
}

bool dragVector(const char* id, float* values, int count, float speed, const char* format) {
  static constexpr const char* kAxes[] = {"X", "Y", "Z", "W"};
  static const ImVec4 kColors[] = {theme::axisX, theme::axisY, theme::axisZ, theme::textDim};
  ImGui::PushID(id);
  const float spacing = 4.0f;
  const float full = ImGui::CalcItemWidth();
  const float each = (full - spacing * (count - 1)) / count;
  bool changed = false;
  for (int i = 0; i < count; ++i) {
    if (i > 0) ImGui::SameLine(0, spacing);
    ImGui::PushID(i);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float letter = 16.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {letter + 4.0f, ImGui::GetStyle().FramePadding.y});
    ImGui::SetNextItemWidth(each);
    changed |= ImGui::DragFloat("##v", &values[i], speed, 0.0f, 0.0f, format);
    ImGui::PopStyleVar();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float h = ImGui::GetFrameHeight();
    draw->AddRectFilled({pos.x, pos.y}, {pos.x + letter, pos.y + h}, theme::u32(kColors[i], 0.18f), theme::radius,
                        ImDrawFlags_RoundCornersLeft);
    ImGui::PushFont(theme::fonts().semibold, theme::sizeSmall);
    const ImVec2 ts = ImGui::CalcTextSize(kAxes[i]);
    draw->AddText({pos.x + (letter - ts.x) * 0.5f, pos.y + (h - ts.y) * 0.5f}, theme::u32(kColors[i]), kAxes[i]);
    ImGui::PopFont();
    ImGui::PopID();
  }
  ImGui::PopID();
  return changed;
}

bool componentHeader(const char* id, const char* icon, const char* title, const std::function<void()>& menu,
                     bool defaultOpen) {
  ImGui::PushID(id);
  ImGuiStorage* storage = ImGui::GetStateStorage();
  const ImGuiID openId = ImGui::GetID("open");
  bool open = storage->GetBool(openId, defaultOpen);

  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;
  const float h = ImGui::GetFrameHeight() + 4.0f;
  const float menuSize = h - 6.0f;
  ImGui::SetNextItemAllowOverlap();
  if (ImGui::InvisibleButton("##header", {width, h})) {
    open = !open;
    storage->SetBool(openId, open);
  }
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(pos, {pos.x + width, pos.y + h}, theme::u32(hovered ? theme::bg3 : theme::bg2), theme::radius);

  const float cy = pos.y + h * 0.5f;
  const char* chevron = open ? ICON_CARET_DOWN : ICON_CARET_RIGHT;
  ImGui::PushFont(nullptr, theme::sizeSmall);
  const ImVec2 cs = ImGui::CalcTextSize(chevron);
  draw->AddText({pos.x + 8, cy - cs.y * 0.5f}, theme::u32(theme::textFaint), chevron);
  ImGui::PopFont();
  const ImVec2 is = ImGui::CalcTextSize(icon);
  draw->AddText({pos.x + 24, cy - is.y * 0.5f}, theme::u32(theme::accent), icon);
  ImGui::PushFont(theme::fonts().semibold, 0.0f);
  const ImVec2 ts = ImGui::CalcTextSize(title);
  draw->AddText({pos.x + 46, cy - ts.y * 0.5f}, theme::u32(theme::text), title);
  ImGui::PopFont();

  if (menu) {
    const ImVec2 back = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos({pos.x + width - menuSize - 3, pos.y + 3});
    if (iconButton("menu", ICON_DOTS_THREE, "More", false, 0, menuSize)) ImGui::OpenPopup("component menu");
    ImGui::SetCursorScreenPos(back);
    if (ImGui::BeginPopup("component menu")) {
      menu();
      ImGui::EndPopup();
    }
  }
  ImGui::PopID();
  if (open) ImGui::Dummy({0, 2});
  return open;
}

bool beginCombo(const char* id, const char* preview) {
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float width = ImGui::CalcItemWidth();
  const bool open = ImGui::BeginCombo(id, preview, ImGuiComboFlags_NoArrowButton);
  const float h = ImGui::GetFrameHeight();
  ImGui::PushFont(nullptr, theme::sizeSmall);
  const ImVec2 cs = ImGui::CalcTextSize(ICON_CARET_DOWN);
  ImGui::GetWindowDrawList()->AddText({pos.x + width - cs.x - 8, pos.y + (h - cs.y) * 0.5f}, theme::u32(theme::textDim), ICON_CARET_DOWN);
  ImGui::PopFont();
  return open;
}

void badge(const char* text, ImVec4 color) {
  ImGui::PushFont(theme::fonts().medium, theme::sizeSmall);
  const ImVec2 ts = ImGui::CalcTextSize(text);
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const ImVec2 pad{6, 2};
  const float lineH = ImGui::GetTextLineHeight();
  ImGui::Dummy({ts.x + pad.x * 2, lineH + pad.y * 2});
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(pos, {pos.x + ts.x + pad.x * 2, pos.y + lineH + pad.y * 2}, theme::u32(color, 0.16f), theme::radius);
  draw->AddText({pos.x + pad.x, pos.y + pad.y}, theme::u32(color), text);
  ImGui::PopFont();
}

int fuzzyScore(std::string_view text, std::string_view query) {
  if (query.empty()) return 0;
  int score = 0, run = 0;
  size_t q = 0;
  for (size_t i = 0; i < text.size() && q < query.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(text[i])) != std::tolower(static_cast<unsigned char>(query[q]))) {
      run = 0;
      continue;
    }
    const bool wordStart = i == 0 || text[i - 1] == ' ' || text[i - 1] == '/' || text[i - 1] == '_' ||
                           text[i - 1] == '.' || (std::isupper(static_cast<unsigned char>(text[i])) && std::islower(static_cast<unsigned char>(text[i - 1])));
    score += 1 + run * 4 + (wordStart ? 6 : 0);
    ++run;
    ++q;
  }
  if (q < query.size()) return -1;
  return score * 100 / static_cast<int>(text.size() + 8);  // shorter matches rank higher
}

void fuzzyText(std::string_view text, std::string_view query, ImU32 color, ImU32 highlight) {
  ImDrawList* draw = ImGui::GetWindowDrawList();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImFont* font = ImGui::GetFont();
  const float size = ImGui::GetFontSize();
  size_t q = 0;
  for (size_t i = 0; i < text.size();) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    const size_t len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    const bool match = q < query.size() && len == 1 &&
                       std::tolower(c) == std::tolower(static_cast<unsigned char>(query[q]));
    if (match) ++q;
    draw->AddText(font, size, pos, match ? highlight : color, text.data() + i, text.data() + i + len);
    pos.x += font->CalcTextSizeA(size, FLT_MAX, 0, text.data() + i, text.data() + i + len).x;
    i += len;
  }
  ImGui::Dummy({pos.x - ImGui::GetCursorScreenPos().x, ImGui::GetTextLineHeight()});
}

void centerNextWindow(ImVec2 size) {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos({vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f},
                          ImGuiCond_Always, {0.5f, 0.5f});
  ImGui::SetNextWindowSize(size, ImGuiCond_Always);
}

namespace {
ImVec2 sBarBottom;  // where the open document bar ends: content starts there, whatever the bar holds
}

float beginDocumentBar(const char* icon, const char* title, const char* subtitle) {
  constexpr float kHeight = 48.0f;
  const ImVec2 a = ImGui::GetCursorScreenPos();
  sBarBottom = {a.x, a.y + kHeight};
  const float width = ImGui::GetContentRegionAvail().x;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(a, {a.x + width, a.y + kHeight}, theme::u32(theme::bg1));
  draw->AddLine({a.x, a.y + kHeight - 1}, {a.x + width, a.y + kHeight - 1}, theme::u32(theme::border));
  ImGui::SetCursorScreenPos({a.x + 16, a.y + (kHeight - 20) * 0.5f});
  ImGui::PushFont(nullptr, 20.0f);
  ImGui::TextColored(theme::accent, "%s", icon);
  ImGui::PopFont();
  ImGui::SameLine(0, 10);
  ImGui::SetCursorScreenPos({ImGui::GetCursorScreenPos().x, a.y + 8});
  ImGui::BeginGroup();
  ImGui::PushFont(theme::fonts().semibold, theme::sizeTitle);
  ImGui::TextUnformatted(title);
  ImGui::PopFont();
  ImGui::PushFont(nullptr, theme::sizeSmall);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3);
  ImGui::TextColored(theme::textFaint, "%s", subtitle);
  ImGui::PopFont();
  ImGui::EndGroup();
  ImGui::SetCursorScreenPos({a.x, a.y + (kHeight - ImGui::GetFrameHeight()) * 0.5f});
  ImGui::Dummy({0, 0});
  ImGui::SameLine();
  return a.x - ImGui::GetWindowPos().x + width - 12;  // window-local, for SameLine
}

void endDocumentBar() {
  ImGui::NewLine();
  ImGui::SetCursorScreenPos(sBarBottom);
  ImGui::Dummy({0, 0});
  ImGui::SetCursorScreenPos(sBarBottom);
}

bool chip(const char* id, const char* label, bool removable, bool* removed, bool keycap) {
  ImGui::PushID(id);
  ImGui::PushFont(theme::fonts().medium, theme::sizeSmall + 0.5f);
  const ImVec2 ts = ImGui::CalcTextSize(label);
  const float h = ImGui::GetFrameHeight() - 2;
  const float closeW = removable ? 16.0f : 0.0f;
  const ImVec2 size{ts.x + 16 + closeW, h};
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const bool pressed = ImGui::InvisibleButton("##chip", size);
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 end{pos.x + size.x, pos.y + size.y};
  if (keycap) {  // a key: lighter top, darker lip at the bottom
    draw->AddRectFilled({pos.x, pos.y + 1}, {end.x, end.y + 1}, theme::u32(theme::bg0), theme::radius);
    draw->AddRectFilled(pos, end, theme::u32(hovered ? theme::bg4 : theme::bg3), theme::radius);
    draw->AddRect(pos, end, theme::u32(theme::border), theme::radius);
  } else {
    draw->AddRectFilled(pos, end, theme::u32(hovered ? theme::bg4 : theme::bg3), h * 0.5f);
  }
  draw->AddText({pos.x + 8, pos.y + (h - ts.y) * 0.5f}, theme::u32(theme::text), label);
  bool clickedClose = false;
  if (removable && hovered) {
    const ImVec2 c{end.x - 11, pos.y + h * 0.5f};
    const bool overClose = ImGui::GetMousePos().x > end.x - closeW - 2;
    draw->AddCircleFilled(c, 7, theme::u32(overClose ? theme::error : theme::bg2, overClose ? 0.9f : 1.0f));
    draw->AddLine({c.x - 3, c.y - 3}, {c.x + 3, c.y + 3}, theme::u32(theme::text), 1.5f);
    draw->AddLine({c.x - 3, c.y + 3}, {c.x + 3, c.y - 3}, theme::u32(theme::text), 1.5f);
    clickedClose = pressed && overClose;
  }
  ImGui::PopFont();
  ImGui::PopID();
  if (removed) *removed = clickedClose;
  return pressed && !clickedClose;
}

bool dismissPressed() { return ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::GetIO().WantTextInput; }

bool colorField(const char* id, float rgba[4]) {
  ImGui::PushID(id);
  auto byte = [](float v) { return static_cast<int>(std::round(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
  char hex[16];
  std::snprintf(hex, sizeof(hex), "#%02X%02X%02X%02X", byte(rgba[0]), byte(rgba[1]), byte(rgba[2]), byte(rgba[3]));
  const float swatch = ImGui::GetFrameHeight();
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - swatch - 4);
  bool changed = false;
  if (ImGui::InputText("##hex", hex, sizeof(hex), ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_CharsUppercase)) {
    std::string digits;
    for (const char* p = hex; *p; ++p) {
      if (std::isxdigit(static_cast<unsigned char>(*p))) digits += *p;
    }
    if (digits.size() == 6 || digits.size() == 8) {
      for (size_t i = 0; i < digits.size() / 2; ++i) rgba[i] = static_cast<float>(std::strtoul(digits.substr(i * 2, 2).c_str(), nullptr, 16)) / 255.0f;
      if (digits.size() == 6) rgba[3] = 1.0f;
      changed = true;
    }
  }
  ImGui::SameLine(0, 4);
  changed |= ImGui::ColorEdit4("##swatch", rgba, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
  ImGui::PopID();
  return changed;
}

}  // namespace ui
