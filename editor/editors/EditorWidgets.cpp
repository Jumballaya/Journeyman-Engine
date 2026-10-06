#include "EditorWidgets.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include <imgui_stdlib.h>

#include "Icons.hpp"
#include "Project.hpp"
#include "TiledFiles.hpp"
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
    ui::dimText(atlas.empty() ? "Choose an atlas first." : "Build the project to see this atlas's regions.");
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

bool imagePopup(const char* id, const Project& project, std::string& value, bool stayOpen,
                const std::function<bool(const std::string&)>& marked) {
  bool picked = false;
  ImGui::SetNextWindowSize({460, 380}, ImGuiCond_Appearing);
  if (!ImGui::BeginPopup(id)) return false;
  static std::string filter;
  if (ImGui::IsWindowAppearing()) {
    filter.clear();
    ImGui::SetKeyboardFocusHere();
  }
  ui::searchField("##imageFilter", filter, "Search images");
  ImGui::BeginChild("##grid");
  const float cell = 64.0f;
  const int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + 6) / (cell + 6)));
  int shown = 0;
  for (const AssetFile& f : project.files()) {
    if (f.kind != AssetKind::Image || f.path.ends_with(".atlas.png")) continue;
    if (!filter.empty() && ui::fuzzyScore(f.path, filter) < 0) continue;
    if (shown++ % columns) ImGui::SameLine(0, 6);
    ImGui::PushID(f.path.c_str());
    const ImVec2 a = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##i", {cell, cell + 16})) {
      value = f.path;
      picked = true;
      if (!stayOpen) ImGui::CloseCurrentPopup();
    }
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const bool on = f.path == value || (marked && marked(f.path));
    draw->AddRectFilled(a, {a.x + cell, a.y + cell}, theme::u32(hovered ? theme::bg4 : theme::bg2), theme::radius);
    if (auto p = Thumbnails::instance().get(project, f.path)) fitted(draw, *p, {a.x + 6, a.y + 6}, {a.x + cell - 6, a.y + cell - 6});
    if (on) {
      draw->AddRect(a, {a.x + cell, a.y + cell}, theme::u32(theme::accent), theme::radius, 2.0f);
      draw->AddText({a.x + cell - 16, a.y + 2}, theme::u32(theme::accent), ICON_CHECK);
    }
    ImGui::PushFont(nullptr, theme::sizeSmall - 1);
    const std::string label = ui::ellipsize(std::filesystem::path(f.path).stem().string(), cell);
    const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
    draw->AddText({a.x + (cell - ts.x) * 0.5f, a.y + cell + 2}, theme::u32(on ? theme::accent : theme::textDim), label.c_str());
    ImGui::PopFont();
    if (hovered) ui::tooltip(f.path.c_str());
    ImGui::PopID();
  }
  if (shown == 0) ui::dimText("No images found: drop PNGs into the Assets panel.");
  ImGui::EndChild();
  ImGui::EndPopup();
  return picked;
}

void properties(const nlohmann::ordered_json& holder, std::string& draft, const PropertyEdit& edit,
                const std::vector<std::string>& skip) {
  using Json = nlohmann::ordered_json;
  const Json props = holder.value("properties", Json::array());
  for (size_t i = 0; i < props.size(); ++i) {
    const std::string name = props[i].value("name", std::string());
    if (std::find(skip.begin(), skip.end(), name) != skip.end()) continue;
    ImGui::PushID(static_cast<int>(i));
    const Json v = props[i].value("value", Json());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(ui::ellipsize(name, 88).c_str());
    ImGui::SameLine(96);
    const float field = ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 4;
    auto set = [&](const Json& value, bool merge) {
      edit("Set " + name, [&](Json& h) { tiled::setProperty(h, name, value); }, merge ? "property:" + name : std::string());
    };
    if (v.is_boolean()) {
      bool on = v.get<bool>();
      if (ui::toggle("##v", &on)) set(on, false);
    } else if (v.is_number_integer()) {
      int n = v.get<int>();
      ImGui::SetNextItemWidth(field);
      if (ImGui::DragInt("##v", &n)) set(n, true);
    } else if (v.is_number()) {
      float n = v.get<float>();
      ImGui::SetNextItemWidth(field);
      if (ImGui::DragFloat("##v", &n, 0.05f)) set(n, true);
    } else {
      std::string text = v.is_string() ? v.get<std::string>() : v.dump();
      ImGui::SetNextItemWidth(field);
      if (ImGui::InputText("##v", &text)) set(text, true);
    }
    ImGui::SameLine(0, 4);
    if (ui::iconButton("remove", ICON_X, "Remove")) edit("Remove " + name, [&](Json& h) { tiled::setProperty(h, name, nullptr); }, {});
    ImGui::PopID();
  }
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70);
  const bool entered = ImGui::InputTextWithHint("##newProperty", "new property", &draft, ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine(0, 4);
  if ((ImGui::Button("Add...", {-FLT_MIN, 0}) || entered) && !draft.empty()) ImGui::OpenPopup("propertyType");
  if (ImGui::BeginPopup("propertyType")) {
    ui::smallText("Its type:", theme::textFaint);
    for (const char* type : {"bool", "int", "float", "string"}) {
      if (!ImGui::Selectable(type)) continue;
      const std::string t = type, name = draft;
      const Json initial = t == "bool" ? Json(true) : t == "int" ? Json(0) : t == "float" ? Json(0.0) : Json("");
      edit("Add " + name, [&](Json& h) { tiled::setProperty(h, name, initial); }, {});
      draft.clear();
    }
    ImGui::EndPopup();
  }
}

std::optional<Thumbnails::Picture> tilePicture(const Project& project, const nlohmann::ordered_json& json,
                                               const std::string& path, uint32_t id) {
  const auto look = tiled::lookOf(json, path, id);
  if (!look) return std::nullopt;
  if (!look->rect) return Thumbnails::instance().get(project, look->image);
  const glm::ivec4 r = *look->rect;
  return Thumbnails::instance().get(project, look->image, {r.x, r.y, r.z, r.w});
}

}  // namespace widgets
