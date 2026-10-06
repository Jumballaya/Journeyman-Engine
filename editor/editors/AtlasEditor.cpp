// The atlas editor (*.atlas.json): the images packed into one texture, as a
// grid to add to (drag images or folders in, or pick them) and remove from,
// the packed result once built, and the packing settings.

#include <map>
#include <set>

#include <imgui.h>
#include <imgui_internal.h>

#include "AssetEditor.hpp"
#include "Editor.hpp"
#include "EditorWidgets.hpp"
#include "Icons.hpp"
#include "Scroll.hpp"
#include "Theme.hpp"
#include "Ui.hpp"

namespace fs = std::filesystem;

namespace {

std::string regionName(const std::string& source) { return fs::path(source).stem().string(); }

// Files mentioning a region ("...atlas.json#name" or "name" in quotes), by region.
class Mentions {
 public:
  const std::set<std::string>& of(const Project& project, const std::string& region) {
    refresh(project);
    static const std::set<std::string> none;
    auto it = _byRegion.find(region);
    return it == _byRegion.end() ? none : it->second;
  }
  void track(std::set<std::string> regions) {
    if (regions != _regions) _regions = std::move(regions), _signature = 0;
  }

 private:
  std::set<std::string> _regions;
  std::map<std::string, std::set<std::string>> _byRegion;
  size_t _signature = 0;

  void refresh(const Project& project) {
    size_t signature = 1;
    for (const AssetFile& f : project.files()) signature = signature * 31 + static_cast<size_t>(f.modified.time_since_epoch().count());
    if (signature == _signature) return;
    _signature = signature;
    _byRegion.clear();
    for (const AssetFile& f : project.files()) {
      const AssetKind k = f.kind;
      if (k != AssetKind::Scene && k != AssetKind::Prefab && k != AssetKind::Ui &&
          k != AssetKind::Script && k != AssetKind::Style) {
        continue;
      }
      if (f.path.find("node_modules") != std::string::npos) continue;
      const std::string text = project.readText(f.path);
      for (const std::string& r : _regions) {
        if (text.find("#" + r + "\"") != std::string::npos || text.find("\"" + r + "\"") != std::string::npos) {
          _byRegion[r].insert(f.path);
        }
      }
    }
  }
};

class AtlasEditor final : public AssetEditor {
 public:
  void draw(Editor& editor, AssetDocument& doc) override;
  bool drawInspector(Editor& editor, AssetDocument& doc) override;
  bool handles(const std::string& command) const override { return command == "edit.delete" && !_selected.empty(); }
  void run(const std::string&, AssetDocument& doc) override { remove(doc, _selected); }

 private:
  std::string _selected;  // a source path
  std::string _filter;
  bool _packedView = false;
  float _packedZoom = 0;  // 0 = fit the view
  std::string _pickFilter;
  std::set<std::string> _picking;  // images ticked in the add dialog
  Mentions _mentions;

  void add(AssetDocument& doc, const std::vector<std::string>& images);
  void remove(AssetDocument& doc, std::string source);
  void drawSources(Editor& editor, AssetDocument& doc, const Json& sources);
  void drawPacked(Editor& editor, AssetDocument& doc);
  void drawAddPopup(Editor& editor, AssetDocument& doc, const Json& sources);
};

void AtlasEditor::add(AssetDocument& doc, const std::vector<std::string>& images) {
  if (images.empty()) return;
  doc.edit(images.size() == 1 ? "Add " + regionName(images[0]) : "Add " + std::to_string(images.size()) + " Images", [&](Json& v) {
    Json& list = v["sources"];
    if (!list.is_array()) list = Json::array();
    for (const std::string& image : images) {
      if (std::find(list.begin(), list.end(), image) == list.end()) list.push_back(image);
    }
  });
  _selected = images.back();
}

void AtlasEditor::remove(AssetDocument& doc, std::string source) {
  doc.edit("Remove " + regionName(source), [&](Json& v) {
    Json& list = v["sources"];
    const auto it = std::find(list.begin(), list.end(), source);
    if (it != list.end()) list.erase(it);
  });
  if (_selected == source) _selected.clear();
}

void AtlasEditor::drawAddPopup(Editor& editor, AssetDocument& doc, const Json& sources) {
  ui::centerNextWindow({520, 560});
  if (!ImGui::BeginPopupModal("Add Images", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) return;
  std::set<std::string> have;
  for (const Json& s : sources) {
    if (s.is_string()) have.insert(s);
  }
  ui::heading("Add Images");
  ui::dimText("Project images not in this atlas yet.");
  ImGui::Dummy({0, 4});
  ui::searchField("##find", _pickFilter, "Filter by name or folder");
  ImGui::Dummy({0, 4});
  ImGui::BeginChild("##list", {0, ImGui::GetContentRegionAvail().y - 48}, ImGuiChildFlags_Borders);
  std::string folder;
  for (const AssetFile& f : editor.project()->files()) {
    if (f.kind != AssetKind::Image || have.contains(f.path)) continue;
    if (!_pickFilter.empty() && ui::fuzzyScore(f.path, _pickFilter) < 0) continue;
    const std::string dir = fs::path(f.path).parent_path().generic_string();
    if (dir != folder) {
      folder = dir;
      ImGui::Dummy({0, 2});
      ui::sectionLabel(folder.c_str());
    }
    ImGui::PushID(f.path.c_str());
    bool on = _picking.contains(f.path);
    if (ImGui::Checkbox("##on", &on)) {
      if (on) _picking.insert(f.path);
      else _picking.erase(f.path);
    }
    ImGui::SameLine();
    if (auto p = Thumbnails::instance().get(*editor.project(), f.path)) {
      const ImVec2 a = ImGui::GetCursorScreenPos();
      ImGui::Dummy({22, 22});
      widgets::fitted(ImGui::GetWindowDrawList(), *p, a, {a.x + 22, a.y + 22});
      ImGui::SameLine();
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(fs::path(f.path).filename().string().c_str());
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::Dummy({0, 4});
  if (ui::button("Cancel", {100, 0}) || ui::dismissPressed()) {
    _picking.clear();
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine(ImGui::GetContentRegionAvail().x - 140 + ImGui::GetCursorPosX());
  ImGui::BeginDisabled(_picking.empty());
  const std::string label = ICON_PLUS "  Add " + std::to_string(_picking.size());
  if (ui::primaryButton(label.c_str(), {140, 0})) {
    add(doc, std::vector<std::string>(_picking.begin(), _picking.end()));
    _picking.clear();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndDisabled();
  ImGui::EndPopup();
}

void AtlasEditor::drawSources(Editor& editor, AssetDocument& doc, const Json& sources) {
  const Project& project = *editor.project();
  if (sources.empty()) {
    ui::emptyState(ICON_SQUARES_FOUR, "No images yet",
                   "An atlas packs many images into one texture. Drag images or a folder here from Assets, or add them.");
  }
  // Region names come from file names, so two files with one name collide.
  std::map<std::string, int> names;
  for (const Json& s : sources) {
    if (s.is_string()) ++names[regionName(s)];
  }
  const float card = 84.0f;
  const int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + 8) / (card + 8)));
  int shown = 0;
  std::string removed;
  for (const Json& s : sources) {
    if (!s.is_string()) continue;
    const std::string source = s;
    const std::string name = regionName(source);
    if (!_filter.empty() && ui::fuzzyScore(source, _filter) < 0) continue;
    if (shown++ % columns) ImGui::SameLine(0, 8);
    ImGui::PushID(source.c_str());
    const ImVec2 a = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##src", {card, card + 18})) _selected = source;
    const bool hovered = ImGui::IsItemHovered();
    const bool selected = source == _selected;
    if (ImGui::BeginDragDropSource()) {  // an image from here drags like one from Assets
      const std::string region = doc.path() + "#" + name;
      ImGui::SetDragDropPayload("JM_ASSET", region.data(), region.size());
      ImGui::TextUnformatted(name.c_str());
      ImGui::EndDragDropSource();
    }
    if (ImGui::BeginPopupContextItem("menu")) {
      _selected = source;
      if (ImGui::MenuItem(ICON_MAGNIFYING_GLASS "  Show in Assets")) editor.revealAsset(source);
      if (ImGui::MenuItem(ICON_X "  Remove from Atlas")) removed = source;
      ImGui::EndPopup();
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(a, {a.x + card, a.y + card + 18}, theme::u32(selected ? theme::bg3 : hovered ? theme::bg2 : theme::bg1), theme::radiusOverlay);
    widgets::checker(draw, {a.x + 5, a.y + 5}, {a.x + card - 5, a.y + card - 5}, 6.0f);
    const bool exists = project.file(source) != nullptr;
    if (auto p = exists ? Thumbnails::instance().get(project, source) : std::nullopt) {
      widgets::fitted(draw, *p, {a.x + 9, a.y + 9}, {a.x + card - 9, a.y + card - 9});
    } else {
      ImGui::PushFont(nullptr, 22.0f);
      const ImVec2 is = ImGui::CalcTextSize(ICON_FILE_X);
      draw->AddText({a.x + (card - is.x) * 0.5f, a.y + (card - is.y) * 0.5f}, theme::u32(theme::error), ICON_FILE_X);
      ImGui::PopFont();
    }
    ImGui::PushFont(nullptr, theme::sizeSmall);
    const std::string label = ui::ellipsize(name, card - 6);
    const ImVec2 ls = ImGui::CalcTextSize(label.c_str());
    const bool clash = names[name] > 1;
    draw->AddText({a.x + (card - ls.x) * 0.5f, a.y + card}, theme::u32(!exists || clash ? theme::error : selected ? theme::text : theme::textDim), label.c_str());
    ImGui::PopFont();
    if (hovered) {
      // Remove on hover, top-right.
      const ImVec2 c{a.x + card - 12, a.y + 12};
      const bool overX = ImLengthSqr(ImVec2(ImGui::GetMousePos().x - c.x, ImGui::GetMousePos().y - c.y)) < 64;
      draw->AddCircleFilled(c, 8, theme::u32(overX ? theme::error : theme::bg0, 0.9f));
      draw->AddLine({c.x - 3, c.y - 3}, {c.x + 3, c.y + 3}, theme::u32(theme::text), 1.5f);
      draw->AddLine({c.x - 3, c.y + 3}, {c.x + 3, c.y - 3}, theme::u32(theme::text), 1.5f);
      if (overX && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) removed = source;
      ui::tooltip(!exists ? (source + " is missing").c_str() : clash ? (name + ": another image has this name, so one hides the other").c_str() : source.c_str());
    }
    if (selected) draw->AddRect(a, {a.x + card, a.y + card + 18}, theme::u32(theme::accent), theme::radiusOverlay, 2.0f);
    ImGui::PopID();
  }
  if (!removed.empty()) remove(doc, removed);
}

void AtlasEditor::drawPacked(Editor& editor, AssetDocument& doc) {
  const Project& project = *editor.project();
  auto packed = Thumbnails::instance().packed(project, doc.path());
  if (!packed) {
    ui::emptyState(ICON_HAMMER, "Not built yet", "The packed texture appears after the next build.");
    return;
  }
  char info[96];
  std::snprintf(info, sizeof(info), "%.0f x %.0f px, %zu regions", packed->image.size.x, packed->image.size.y, packed->regions.size());
  ui::smallText((std::string(info) + "    Scroll to zoom").c_str(), theme::textFaint);
  ImGui::BeginChild("##packedView", {0, 0}, 0, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  // Floored like the zoom: a zero-sized button (a collapsed view) would assert.
  const float fit = std::max(0.1f, std::min(avail.x / packed->image.size.x, (avail.y - 8) / packed->image.size.y));
  if (const float factor = ImGui::IsWindowHovered() ? scroll::canvasGesture().zoom : 1.0f; factor != 1.0f) {
    _packedZoom = std::clamp((_packedZoom > 0 ? _packedZoom : fit) * factor, 0.1f, 16.0f);
  }
  const float wanted = _packedZoom > 0 ? _packedZoom : fit;
  const float scale = wanted >= 1.0f ? std::floor(wanted) : wanted;
  const ImVec2 size{packed->image.size.x * scale, packed->image.size.y * scale};
  const ImVec2 a = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##packed", size);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  widgets::checker(draw, a, {a.x + size.x, a.y + size.y});
  draw->AddImage(packed->image.texture, a, {a.x + size.x, a.y + size.y});
  // Outline the region under the mouse (and the selected one); click selects its source.
  const ImVec2 m = ImGui::GetMousePos();
  for (const auto& [name, r] : packed->regions) {
    const ImVec2 r0{a.x + r[0] * scale, a.y + r[1] * scale}, r1{r0.x + r[2] * scale, r0.y + r[3] * scale};
    const bool over = ImGui::IsItemHovered() && m.x >= r0.x && m.x < r1.x && m.y >= r0.y && m.y < r1.y;
    const bool selected = regionName(_selected) == name;
    if (over || selected) draw->AddRect(r0, r1, theme::u32(selected ? theme::accent : theme::text, selected ? 1.0f : 0.7f), 0, 1.5f);
    if (over) {
      ui::tooltip(name.c_str());
      if (ImGui::IsItemClicked()) {
        for (const Json& s : doc.value().value("sources", Json::array())) {
          if (s.is_string() && regionName(s) == name) _selected = s;
        }
      }
    }
  }
  ImGui::EndChild();
}

void AtlasEditor::draw(Editor& editor, AssetDocument& doc) {
  const Json& value = doc.value();
  const Json sources = value.value("sources", Json::array());
  std::set<std::string> regions;
  for (const Json& s : sources) {
    if (s.is_string()) regions.insert(regionName(s));
  }
  _mentions.track(regions);

  const float right = ui::beginDocumentBar(ICON_SQUARES_FOUR, "Atlas", doc.path().c_str());
  const float viewW = 170, filterW = 110, padW = 110, searchW = 180, addW = 130;
  ImGui::SameLine(right - (viewW + filterW + padW + searchW + addW + 32));
  // Sources | Packed.
  for (int v = 0; v < 2; ++v) {
    ImGui::PushStyleColor(ImGuiCol_Button, _packedView == (v == 1) ? theme::bg3 : theme::withAlpha(theme::bg3, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, _packedView == (v == 1) ? theme::text : theme::textDim);
    if (ImGui::Button(v == 0 ? "Images" : "Packed", {viewW * 0.5f - 2, 0})) _packedView = v == 1;
    ImGui::PopStyleColor(2);
    ImGui::SameLine(0, 4);
  }
  ImGui::SameLine(0, 8);
  ui::searchField("filter", _filter, "Filter", searchW);
  ImGui::SameLine(0, 8);
  const std::string filter = value.value("filter", std::string("nearest"));
  ImGui::SetNextItemWidth(filterW);
  if (ui::beginCombo("##filter", filter == "linear" ? "Smooth" : "Pixel art")) {
    if (ImGui::Selectable("Pixel art (nearest)", filter != "linear")) doc.edit("Set Filter", [](Json& v) { v["filter"] = "nearest"; });
    if (ImGui::Selectable("Smooth (linear)", filter == "linear")) doc.edit("Set Filter", [](Json& v) { v["filter"] = "linear"; });
    ImGui::EndCombo();
  }
  ui::tooltip("How the texture is sampled when scaled");
  ImGui::SameLine(0, 8);
  int padding = value.value("padding", 0);
  ImGui::SetNextItemWidth(padW);
  if (ImGui::DragInt("##padding", &padding, 0.1f, 0, 16, "Padding %d px")) {
    doc.edit("Set Padding", [&](Json& v) { v["padding"] = padding; }, "padding");
  }
  ui::tooltip("Empty pixels around each image, so neighbors don't bleed in at sub-pixel positions");
  ImGui::SameLine(0, 8);
  if (ui::primaryButton(ICON_PLUS "  Add Images", {addW, 0})) ImGui::OpenPopup("Add Images");
  drawAddPopup(editor, doc, sources);
  ui::endDocumentBar();

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16, 12});
  ImGui::BeginChild("##body", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
  if (_packedView) {
    drawPacked(editor, doc);
  } else {
    drawSources(editor, doc, sources);
    ImGui::Dummy({0, 8});
    ui::smallText("Drag images or folders here from Assets. Changes pack on the next build.", theme::textFaint);
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
  // The whole tab takes dropped images and folders.
  if (ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->InnerRect, ImGui::GetID("##drop"))) {
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("JM_ASSET")) {
      const std::string path(static_cast<const char*>(p->Data), static_cast<size_t>(p->DataSize));
      if (assetKindOf(path) == AssetKind::Image) add(doc, {path});
    }
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("JM_FOLDER")) {
      const std::string folder(static_cast<const char*>(p->Data), static_cast<size_t>(p->DataSize));
      std::vector<std::string> images;
      for (const AssetFile& f : editor.project()->files()) {
        if (f.kind == AssetKind::Image && f.path.starts_with(folder + "/")) images.push_back(f.path);
      }
      add(doc, images);
    }
    ImGui::EndDragDropTarget();
  }
}

bool AtlasEditor::drawInspector(Editor& editor, AssetDocument& doc) {
  const Project& project = *editor.project();
  const Json sources = doc.value().value("sources", Json::array());
  const bool present = std::any_of(sources.begin(), sources.end(), [&](const Json& s) { return s == _selected; });
  if (_selected.empty() || !present) {
    ui::emptyState(ICON_SQUARES_FOUR, "Pick an image", "Select an image in the atlas to see where it's used.");
    return true;
  }
  const std::string name = regionName(_selected);
  ImGui::PushFont(nullptr, 20.0f);
  ImGui::TextColored(theme::accent, ICON_IMAGE);
  ImGui::PopFont();
  ImGui::SameLine(0, 10);
  ImGui::BeginGroup();
  ImGui::PushFont(theme::fonts().semibold, theme::sizeTitle);
  ImGui::TextUnformatted(name.c_str());
  ImGui::PopFont();
  ui::smallText(_selected.c_str(), theme::textFaint);
  ImGui::EndGroup();
  ImGui::Dummy({0, 6});
  const float w = ImGui::GetContentRegionAvail().x;
  const ImVec2 a = ImGui::GetCursorScreenPos();
  ImGui::Dummy({w, 180});
  ImDrawList* draw = ImGui::GetWindowDrawList();
  widgets::checker(draw, a, {a.x + w, a.y + 180});
  if (auto p = Thumbnails::instance().get(project, _selected)) {
    widgets::fitted(draw, *p, {a.x + 12, a.y + 12}, {a.x + w - 12, a.y + 168});
    char size[48];
    std::snprintf(size, sizeof(size), "%.0f x %.0f px", p->size.x, p->size.y);
    ui::smallText(size, theme::textFaint);
  }
  ImGui::Dummy({0, 6});
  ui::sectionLabel("Reference");
  const std::string reference = doc.path() + "#" + name;
  ImGui::PushFont(theme::fonts().mono, theme::sizeSmall);
  ImGui::TextWrapped("%s", reference.c_str());
  ImGui::PopFont();
  if (ui::button(ICON_COPY "  Copy Reference", {w, 0})) ImGui::SetClipboardText(reference.c_str());
  ImGui::Dummy({0, 6});
  const auto& users = _mentions.of(project, name);
  ui::sectionLabel(users.empty() ? "Not used by name" : ("Used in " + std::to_string(users.size()) + (users.size() == 1 ? " file" : " files")).c_str());
  for (const std::string& u : users) {
    ImGui::PushID(u.c_str());
    if (ImGui::Selectable((std::string(assetKindInfo(assetKindOf(u)).icon) + "  " + u).c_str())) editor.revealAsset(u);
    ImGui::PopID();
  }
  if (users.empty()) ui::smallText("Scripts may still build its name at run time.", theme::textFaint);
  ImGui::Dummy({0, 10});
  if (ui::button(ICON_X "  Remove from Atlas", {w, 0})) remove(doc, _selected);
  return true;
}

}  // namespace

std::unique_ptr<AssetEditor> makeAtlasEditor() { return std::make_unique<AtlasEditor>(); }
