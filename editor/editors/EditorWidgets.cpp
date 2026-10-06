#include "EditorWidgets.hpp"

#include <algorithm>
#include <cmath>

#include <imgui_stdlib.h>

#include "Icons.hpp"
#include "Project.hpp"
#include "Theme.hpp"
#include "Ui.hpp"

namespace widgets {

void checker(ImDrawList* draw, ImVec2 a, ImVec2 b, float cell) {
  draw->AddRectFilled(a, b, theme::u32(theme::bg2));
  draw->PushClipRect(a, b, true);
  for (float y = a.y; y < b.y; y += cell) {
    for (float x = a.x + (static_cast<int>((y - a.y) / cell) % 2 ? cell : 0); x < b.x; x += cell * 2) {
      draw->AddRectFilled({x, y}, {x + cell, y + cell}, theme::u32(theme::bg3));
    }
  }
  draw->PopClipRect();
}

void fitted(ImDrawList* draw, const Thumbnails::Picture& picture, ImVec2 a, ImVec2 b, float alpha) {
  const float w = b.x - a.x, h = b.y - a.y;
  if (picture.size.x <= 0 || picture.size.y <= 0 || w <= 0 || h <= 0) return;
  const float fit = std::min(w / picture.size.x, h / picture.size.y);
  const float scale = fit >= 1.0f ? std::floor(fit) : fit;
  const ImVec2 s{picture.size.x * scale, picture.size.y * scale};
  const ImVec2 p{std::round(a.x + (w - s.x) * 0.5f), std::round(a.y + (h - s.y) * 0.5f)};
  draw->AddImage(picture.texture, p, {p.x + s.x, p.y + s.y}, picture.uv0, picture.uv1,
                 ImGui::ColorConvertFloat4ToU32({1, 1, 1, alpha}));
}

bool regionPopup(const char* id, const Project& project, const std::string& atlas, std::string& value) {
  bool picked = false;
  ImGui::SetNextWindowSize({420, 380});
  if (!ImGui::BeginPopup(id)) return false;
  static std::string filter;
  if (ImGui::IsWindowAppearing()) {
    filter.clear();
    ImGui::SetKeyboardFocusHere();
  }
  ui::searchField("##regionFilter", filter, "Search regions");
  const auto regions = Thumbnails::instance().regions(project, atlas);
  if (regions.empty()) {
    ImGui::Dummy({0, 20});
    ui::dimText(atlas.empty() ? "Choose the tileset's atlas first." : "Build the project to see this atlas's regions.");
  }
  ImGui::BeginChild("##grid");
  const float cell = 64.0f;
  const int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + 6) / (cell + 6)));
  int shown = 0;
  for (const std::string& region : regions) {
    if (!filter.empty() && ui::fuzzyScore(region, filter) < 0) continue;
    if (shown++ % columns) ImGui::SameLine(0, 6);
    ImGui::PushID(region.c_str());
    const ImVec2 a = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##r", {cell, cell + 16})) {
      value = region;
      picked = true;
      ImGui::CloseCurrentPopup();
    }
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const bool current = region == value;
    draw->AddRectFilled(a, {a.x + cell, a.y + cell}, theme::u32(hovered ? theme::bg4 : theme::bg2), theme::radius);
    if (auto p = Thumbnails::instance().get(project, atlas + "#" + region)) fitted(draw, *p, {a.x + 6, a.y + 6}, {a.x + cell - 6, a.y + cell - 6});
    if (current) draw->AddRect(a, {a.x + cell, a.y + cell}, theme::u32(theme::accent), theme::radius, 2.0f);
    ImGui::PushFont(nullptr, theme::sizeSmall - 1);
    const std::string label = ui::ellipsize(region, cell);
    const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
    draw->AddText({a.x + (cell - ts.x) * 0.5f, a.y + cell + 2}, theme::u32(current ? theme::accent : theme::textDim), label.c_str());
    ImGui::PopFont();
    if (hovered) ui::tooltip(region.c_str());
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::EndPopup();
  return picked;
}

bool regionField(const char* id, const Project& project, const std::string& atlas, std::string& value, float reserve,
                 const char* hint) {
  ImGui::PushID(id);
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - reserve - ImGui::GetFrameHeight() - 4);
  bool changed = ImGui::InputTextWithHint("##text", hint, &value);
  ImGui::SameLine(0, 4);
  if (ui::iconButton("pick", ICON_SQUARES_FOUR, "Pick from the atlas")) ImGui::OpenPopup("regions");
  changed |= regionPopup("regions", project, atlas, value);
  ImGui::PopID();
  return changed;
}

}  // namespace widgets
