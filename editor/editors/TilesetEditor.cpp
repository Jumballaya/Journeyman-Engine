// The tileset editor (Tiled .tsj): the tiles as cards, made from images or cut
// from a sheet; the selected tile's type, solidity, properties and animation;
// and terrains (Tiled wang sets), painted onto the cards' edges and corners
// for the scene's terrain brush. Files stay Tiled's own.

#include <cmath>
#include <filesystem>
#include <map>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "AssetEditor.hpp"
#include "Editor.hpp"
#include "EditorWidgets.hpp"
#include "Icons.hpp"
#include "Theme.hpp"
#include "TiledFiles.hpp"
#include "Ui.hpp"

namespace fs = std::filesystem;

namespace {

constexpr const char* kTerrainTypes[] = {"corner", "edge", "mixed"};
constexpr const char* kPalette[] = {"#ff4caf50", "#ff2196f3", "#ffff9800", "#ff9c27b0", "#fff44336", "#ff795548", "#ff00bcd4", "#ffcddc39"};

ImVec4 hexColor(const std::string& hex) {
  std::string h = hex.starts_with('#') ? hex.substr(1) : hex;
  if (h.size() != 6 && h.size() != 8) return theme::accent;
  auto byte = [&](size_t at) { return static_cast<float>(std::stoi(h.substr(at, 2), nullptr, 16)) / 255.0f; };
  const size_t rgb = h.size() == 8 ? 2 : 0;
  return {byte(rgb), byte(rgb + 2), byte(rgb + 4), 1.0f};
}

std::string colorHex(const float c[3]) {
  char out[10];
  std::snprintf(out, sizeof(out), "#%02x%02x%02x", static_cast<int>(c[0] * 255 + 0.5f), static_cast<int>(c[1] * 255 + 0.5f),
                static_cast<int>(c[2] * 255 + 0.5f));
  return out;
}

// Where a terrain color sits on a tile: positions of Tiled's wang id
// (0 top, 1 top-right, 2 right... 7 top-left) a set of this type uses.
bool usesPosition(const std::string& type, int position) {
  return type == "mixed" || (type == "edge") == (position % 2 == 0);
}

// The wang position under a point of a card (u, v in 0..1, v down), or -1.
int positionAt(const std::string& type, float u, float v) {
  if (type == "corner") return u >= 0.5f ? (v < 0.5f ? 1 : 3) : (v < 0.5f ? 7 : 5);
  if (type == "edge") {
    const bool aboveMain = v < u, aboveAnti = v < 1.0f - u;
    return aboveMain ? (aboveAnti ? 0 : 2) : (aboveAnti ? 6 : 4);
  }
  const int col = std::clamp(static_cast<int>(u * 3), 0, 2), row = std::clamp(static_cast<int>(v * 3), 0, 2);
  constexpr int kGrid[3][3] = {{7, 0, 1}, {6, -1, 2}, {5, 4, 3}};
  return kGrid[row][col];
}

// The area of a card a wang position covers, as a polygon.
std::vector<ImVec2> positionShape(const std::string& type, int position, ImVec2 a, ImVec2 b) {
  const ImVec2 c{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
  const ImVec2 corners[4] = {{b.x, a.y}, {b.x, b.y}, {a.x, b.y}, {a.x, a.y}};  // top-right, bottom-right, bottom-left, top-left
  if (type == "corner") {
    const ImVec2 k = corners[(position - 1) / 2];
    return {{k.x, k.y}, {c.x, k.y}, c, {k.x, c.y}};
  }
  if (type == "edge") {
    const int e = position / 2;  // top, right, bottom, left
    return {corners[(e + 3) % 4], corners[e], c};
  }
  const float w = (b.x - a.x) / 3, h = (b.y - a.y) / 3;
  constexpr int kCol[8] = {1, 2, 2, 2, 1, 0, 0, 0}, kRow[8] = {0, 0, 1, 2, 2, 2, 1, 0};
  const ImVec2 p{a.x + kCol[position] * w, a.y + kRow[position] * h};
  return {p, {p.x + w, p.y}, {p.x + w, p.y + h}, {p.x, p.y + h}};
}

class TilesetEditor final : public AssetEditor {
 public:
  void draw(Editor& editor, AssetDocument& doc) override;
  bool drawInspector(Editor& editor, AssetDocument& doc) override;
  void show(const std::string& item) override { _selected = static_cast<uint32_t>(std::atoi(item.c_str())); }
  bool handles(const std::string& command) const override { return command == "edit.delete" && _hasSelection; }
  void run(const std::string& command, AssetDocument& doc) override {
    if (command == "edit.delete") removeTile(doc, _selected);
  }

 private:
  uint32_t _selected = 0;
  bool _hasSelection = false;
  std::string _filter;
  int _terrain = -1;  // the wang set being painted onto the cards, or -1
  int _color = 1;     // its color the cards get (1-based)
  std::string _newProperty;
  // Tiles placed in the project's maps, by id (rescanned when maps change).
  std::map<uint32_t, size_t> _placed;
  std::vector<std::string> _maps;
  size_t _placedSignature = 1;

  void countPlaced(Editor& editor, const std::string& path);
  void drawTerrainBar(AssetDocument& doc);
  void drawCards(Editor& editor, AssetDocument& doc);
  void drawCardTerrain(AssetDocument& doc, uint32_t id, ImVec2 a, ImVec2 b, bool hovered);
  void tileInspector(Editor& editor, AssetDocument& doc);
  void terrainInspector(AssetDocument& doc);
  void removeTile(AssetDocument& doc, uint32_t id);
  void editTile(AssetDocument& doc, const std::string& label, const std::function<void(Json&)>& mutate, const std::string& mergeKey = {});
};

void TilesetEditor::countPlaced(Editor& editor, const std::string& path) {
  const Project& project = *editor.project();
  size_t signature = 7;
  for (const AssetFile& f : project.files()) {
    if (f.kind == AssetKind::Map) signature = signature * 31 + static_cast<size_t>(f.modified.time_since_epoch().count());
  }
  if (signature == _placedSignature) return;
  _placedSignature = signature;
  _placed.clear();
  _maps.clear();
  for (const AssetFile& f : project.files()) {
    if (f.kind != AssetKind::Map) continue;
    const Json* map = editor.map(f.path);
    if (!map) continue;
    const auto refs = tiled::tilesets(*map, f.path);
    for (size_t i = 0; i < refs.size(); ++i) {
      if (refs[i].path != path) continue;
      _maps.push_back(f.path);
      const uint32_t first = refs[i].firstGid, end = i + 1 < refs.size() ? refs[i + 1].firstGid : ~0u;
      for (const Json& layer : map->value("layers", Json::array())) {
        for (const Json& g : layer.value("data", Json::array())) {
          const uint32_t gid = g.get<uint32_t>() & ~tiled::kFlags;
          if (gid >= first && gid < end) ++_placed[gid - first];
        }
      }
    }
  }
}

void TilesetEditor::editTile(AssetDocument& doc, const std::string& label, const std::function<void(Json&)>& mutate,
                             const std::string& mergeKey) {
  const uint32_t id = _selected;
  doc.edit(label, [&](Json& v) { mutate(*tiled::tileEntry(v, id, true)); }, mergeKey);
}

void TilesetEditor::removeTile(AssetDocument& doc, uint32_t id) {
  if (!doc.value().value("image", std::string()).empty()) return;  // a sheet's cells come from the image
  doc.edit("Delete Tile", [&](Json& v) {
    Json& tiles = v["tiles"];
    for (size_t i = 0; i < tiles.size(); ++i) {
      if (tiles[i].value("id", ~0u) == id) tiles.erase(i--);
    }
    v["tilecount"] = tiles.size();
    // Animations and terrains stop naming it.
    for (Json& t : tiles) {
      if (!t.contains("animation")) continue;
      Json kept = Json::array();
      for (const Json& f : t["animation"]) {
        if (f.value("tileid", ~0u) != id) kept.push_back(f);
      }
      if (kept.empty()) t.erase("animation");
      else t["animation"] = kept;
    }
    if (!v.contains("wangsets")) return;
    for (Json& ws : v["wangsets"]) {
      Json kept = Json::array();
      for (const Json& w : ws.value("wangtiles", Json::array())) {
        if (w.value("tileid", ~0u) != id) kept.push_back(w);
      }
      ws["wangtiles"] = kept;
    }
  });
  _hasSelection = false;
}

void TilesetEditor::drawTerrainBar(AssetDocument& doc) {
  const Json sets = doc.value().value("wangsets", Json::array());
  ImGui::AlignTextToFramePadding();
  ui::smallText("Terrains", theme::textDim);
  ImGui::SameLine(0, 10);
  for (size_t i = 0; i < sets.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    const bool on = _terrain == static_cast<int>(i);
    if (ui::chip("set", sets[i].value("name", std::string("Terrain")).c_str())) _terrain = on ? -1 : static_cast<int>(i), _color = 1;
    if (on) {
      const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
      ImGui::GetWindowDrawList()->AddRect(a, b, theme::u32(theme::accent), theme::radius, 2.0f);
    }
    ImGui::PopID();
    ImGui::SameLine(0, 6);
  }
  if (ui::iconButton("addTerrain", ICON_PLUS, "New terrain: tiles that join up as you paint (paths, walls, water edges)")) {
    const int index = static_cast<int>(sets.size());
    doc.edit("Add Terrain", [&](Json& v) {
      if (!v.contains("wangsets")) v["wangsets"] = Json::array();
      v["wangsets"].push_back({{"name", "Terrain " + std::to_string(index + 1)}, {"type", "edge"}, {"tile", -1},
                               {"colors", Json::array({{{"name", "Terrain"}, {"color", kPalette[index % 8]}, {"probability", 1}, {"tile", -1}}})},
                               {"wangtiles", Json::array()}});
    });
    _terrain = index;
    _color = 1;
  }
  if (_terrain < 0 || _terrain >= static_cast<int>(sets.size())) {
    _terrain = -1;
    return;
  }
  // The colors to paint with.
  const Json colors = sets[static_cast<size_t>(_terrain)].value("colors", Json::array());
  ImGui::SameLine(0, 16);
  ui::smallText("Paint:", theme::textFaint);
  for (size_t c = 0; c < colors.size(); ++c) {
    ImGui::SameLine(0, 6);
    ImGui::PushID(static_cast<int>(c) + 100);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    const std::string name = colors[c].value("name", std::string());
    const float w = ImGui::CalcTextSize(name.c_str()).x + h + 10;
    if (ImGui::InvisibleButton("color", {w, h})) _color = static_cast<int>(c) + 1;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const bool on = _color == static_cast<int>(c) + 1;
    draw->AddRectFilled(p, {p.x + w, p.y + h}, theme::u32(on ? theme::bg4 : theme::bg2), theme::radius);
    draw->AddRectFilled({p.x + 5, p.y + 5}, {p.x + h - 5, p.y + h - 5}, ImGui::GetColorU32(hexColor(colors[c].value("color", std::string()))), 3.0f);
    draw->AddText({p.x + h, p.y + (h - ImGui::GetFontSize()) * 0.5f}, theme::u32(on ? theme::text : theme::textDim), name.c_str());
    if (on) draw->AddRect(p, {p.x + w, p.y + h}, theme::u32(theme::accent), theme::radius, 1.5f);
    ImGui::PopID();
  }
  ImGui::SameLine(0, 12);
  ui::smallText("Click a tile's edges or corners to paint them; right-click clears.", theme::textFaint);
}

void TilesetEditor::drawCardTerrain(AssetDocument& doc, uint32_t id, ImVec2 a, ImVec2 b, bool hovered) {
  const Json& ws = doc.value()["wangsets"][static_cast<size_t>(_terrain)];
  const std::string type = ws.value("type", std::string("corner"));
  const Json colors = ws.value("colors", Json::array());
  Json wangid = Json::array({0, 0, 0, 0, 0, 0, 0, 0});
  for (const Json& w : ws.value("wangtiles", Json::array())) {
    if (w.value("tileid", ~0u) == id && w.value("wangid", Json()).size() == 8) wangid = w["wangid"];
  }
  ImDrawList* draw = ImGui::GetWindowDrawList();
  for (int k = 0; k < 8; ++k) {
    const int c = wangid[static_cast<size_t>(k)].get<int>();
    if (!c || !usesPosition(type, k) || c > static_cast<int>(colors.size())) continue;
    ImVec4 color = hexColor(colors[static_cast<size_t>(c - 1)].value("color", std::string()));
    color.w = 0.55f;
    const auto shape = positionShape(type, k, a, b);
    draw->AddConvexPolyFilled(shape.data(), static_cast<int>(shape.size()), ImGui::GetColorU32(color));
  }
  if (!hovered) return;
  const ImVec2 m = ImGui::GetMousePos();
  const int position = positionAt(type, (m.x - a.x) / (b.x - a.x), (m.y - a.y) / (b.y - a.y));
  if (position < 0 || !usesPosition(type, position)) return;
  const auto shape = positionShape(type, position, a, b);
  draw->AddPolyline(shape.data(), static_cast<int>(shape.size()), theme::u32(theme::text), 1.5f, ImDrawFlags_Closed);
  const bool paint = ImGui::IsMouseDown(ImGuiMouseButton_Left), clear = ImGui::IsMouseDown(ImGuiMouseButton_Right);
  const int value = paint ? _color : 0;
  if ((!paint && !clear) || wangid[static_cast<size_t>(position)].get<int>() == value) return;
  const int set = _terrain;
  doc.edit("Paint Terrain", [&](Json& v) {
    Json& tiles = v["wangsets"][static_cast<size_t>(set)]["wangtiles"];
    auto it = std::find_if(tiles.begin(), tiles.end(), [&](const Json& w) { return w.value("tileid", ~0u) == id; });
    if (it == tiles.end()) it = tiles.insert(tiles.end(), Json{{"tileid", id}, {"wangid", {0, 0, 0, 0, 0, 0, 0, 0}}});
    (*it)["wangid"][static_cast<size_t>(position)] = value;
    if (std::all_of((*it)["wangid"].begin(), (*it)["wangid"].end(), [](const Json& c) { return c == 0; })) tiles.erase(it);
  }, gestureKey("terrainPaint", ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)));
}

void TilesetEditor::drawCards(Editor& editor, AssetDocument& doc) {
  const Project& project = *editor.project();
  const Json& value = doc.value();
  const std::vector<uint32_t> ids = tiled::tileIds(value);
  const float card = _terrain >= 0 ? 84.0f : 76.0f;
  const int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + 8) / (card + 8)));
  int shown = 0;
  for (uint32_t id : ids) {
    const std::string type = tiled::typeOf(value, id);
    if (!_filter.empty() && ui::fuzzyScore(type, _filter) < 0) continue;
    if (shown++ % columns) ImGui::SameLine(0, 8);
    ImGui::PushID(static_cast<int>(id));
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 imageA{a.x + 6, a.y + 6}, imageB{a.x + card - 6, a.y + card - 6};
    ImGui::InvisibleButton("##tile", {card, card + 18}, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    if (_terrain < 0 && ImGui::IsItemClicked()) _selected = id, _hasSelection = true;
    if (_terrain < 0 && ImGui::BeginPopupContextItem("tileMenu")) {
      _selected = id, _hasSelection = true;
      if (ImGui::MenuItem(ICON_TRASH "  Delete", nullptr, false, value.value("image", std::string()).empty())) removeTile(doc, id);
      ImGui::EndPopup();
    }
    const bool selected = _hasSelection && id == _selected && _terrain < 0;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(a, {a.x + card, a.y + card + 18}, theme::u32(selected ? theme::bg3 : hovered ? theme::bg2 : theme::bg1), theme::radiusOverlay);
    widgets::checker(draw, imageA, imageB, 6.0f);
    const Json* entry = tiled::tileEntry(value, id);
    // An animated tile shows its frames as the map will.
    uint32_t look = id;
    if (entry && entry->contains("animation") && !(*entry)["animation"].empty()) {
      const Json& frames = (*entry)["animation"];
      int total = 0;
      for (const Json& f : frames) total += std::max(1, f.value("duration", 100));
      int at = static_cast<int>(std::fmod(ImGui::GetTime() * 1000.0, std::max(1, total)));
      for (const Json& f : frames) {
        if ((at -= std::max(1, f.value("duration", 100))) < 0) {
          look = f.value("tileid", id);
          break;
        }
      }
    }
    if (auto p = widgets::tilePicture(project, value, doc.path(), look)) widgets::fitted(draw, *p, imageA, imageB);
    if (_terrain >= 0) drawCardTerrain(doc, id, imageA, imageB, hovered);
    // Behavior badges.
    std::string flags;
    if (entry && tiled::property(*entry, "solid") == Json(true)) flags += ICON_PROHIBIT;
    if (entry && entry->contains("animation")) flags += ICON_FILM_STRIP;
    if (!flags.empty() && _terrain < 0) {
      ImGui::PushFont(nullptr, theme::sizeSmall);
      const ImVec2 fs = ImGui::CalcTextSize(flags.c_str());
      draw->AddRectFilled({a.x + card - fs.x - 10, a.y + 3}, {a.x + card - 3, a.y + fs.y + 6}, theme::u32(theme::bg0, 0.85f), theme::radius);
      draw->AddText({a.x + card - fs.x - 7, a.y + 4}, theme::u32(theme::textDim), flags.c_str());
      ImGui::PopFont();
    }
    if (auto it = _placed.find(id); it != _placed.end() && _terrain < 0 && hovered) {
      ui::tooltip(("Placed " + std::to_string(it->second) + " times").c_str());
    }
    ImGui::PushFont(nullptr, theme::sizeSmall);
    const std::string label = ui::ellipsize(type.empty() ? "#" + std::to_string(id) : type, card - 6);
    const ImVec2 ls = ImGui::CalcTextSize(label.c_str());
    draw->AddText({a.x + (card - ls.x) * 0.5f, a.y + card}, theme::u32(type.empty() ? theme::textFaint : selected ? theme::text : theme::textDim), label.c_str());
    ImGui::PopFont();
    if (selected) draw->AddRect(a, {a.x + card, a.y + card + 18}, theme::u32(theme::accent), theme::radiusOverlay, 2.0f);
    ImGui::PopID();
  }
  if (!value.value("image", std::string()).empty() || _terrain >= 0) return;
  // The add card: images become tiles, one per click.
  if (shown % columns) ImGui::SameLine(0, 8);
  const ImVec2 a = ImGui::GetCursorScreenPos();
  if (ImGui::InvisibleButton("##add", {card, card + 18})) ImGui::OpenPopup("addImages");
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRect(a, {a.x + card, a.y + card + 18}, theme::u32(hovered ? theme::accent : theme::border), theme::radiusOverlay, 1.5f);
  ImGui::PushFont(nullptr, 22.0f);
  const ImVec2 ps = ImGui::CalcTextSize(ICON_PLUS);
  draw->AddText({a.x + (card - ps.x) * 0.5f, a.y + (card - ps.y) * 0.5f}, theme::u32(hovered ? theme::accent : theme::textDim), ICON_PLUS);
  ImGui::PopFont();
  ImGui::PushFont(nullptr, theme::sizeSmall);
  const ImVec2 ts = ImGui::CalcTextSize("Add Tiles");
  draw->AddText({a.x + (card - ts.x) * 0.5f, a.y + card}, theme::u32(theme::textDim), "Add Tiles");
  ImGui::PopFont();
  if (hovered) ui::tooltip("Pick images: each becomes a tile");
  std::string image;
  auto added = [&](const std::string& path) {
    for (uint32_t id : ids) {
      if (auto look = tiled::lookOf(value, doc.path(), id); look && look->image == path) return true;
    }
    return false;
  };
  if (widgets::imagePopup("addImages", project, image, true, added) && !added(image)) {
    const auto picture = Thumbnails::instance().get(project, image);
    const uint32_t id = tiled::tileSpan(value);
    doc.edit("Add Tile " + fs::path(image).stem().string(), [&](Json& v) {
      Json& t = *tiled::tileEntry(v, id, true);
      t["image"] = tiled::relativeTo(doc.path(), image);
      if (picture) t["imagewidth"] = static_cast<int>(picture->size.x), t["imageheight"] = static_cast<int>(picture->size.y);
      t["type"] = fs::path(image).stem().string();
      v["tilecount"] = v["tiles"].size();
    });
    _selected = id, _hasSelection = true;
  }
}

void TilesetEditor::draw(Editor& editor, AssetDocument& doc) {
  const Project& project = *editor.project();
  countPlaced(editor, doc.path());
  const Json& value = doc.value();
  const bool sheet = !value.value("image", std::string()).empty();

  const float right = ui::beginDocumentBar(ICON_GRID_FOUR, "Tileset", doc.path().c_str());
  const float searchW = 150.0f, sizeW = 110.0f, buttons = 2 * (ImGui::GetFrameHeight() + 4);
  ImGui::SameLine(right - (searchW + sizeW + buttons + 16));
  ui::searchField("filter", _filter, "Find a tile type", searchW);
  ImGui::SameLine(0, 8);
  int size[2] = {value.value("tilewidth", 16), value.value("tileheight", 16)};
  ImGui::SetNextItemWidth(sizeW);
  if (ImGui::InputInt2("##tileSize", size, ImGuiInputTextFlags_EnterReturnsTrue) && size[0] > 0 && size[1] > 0) {
    doc.edit("Set Tile Size", [&](Json& v) {
      v["tilewidth"] = size[0];
      v["tileheight"] = size[1];
      if (!sheet) return;
      const int columns = std::max(1, (v.value("imagewidth", 0) - 2 * v.value("margin", 0) + v.value("spacing", 0)) / (size[0] + v.value("spacing", 0)));
      const int rows = std::max(1, (v.value("imageheight", 0) - 2 * v.value("margin", 0) + v.value("spacing", 0)) / (size[1] + v.value("spacing", 0)));
      v["columns"] = columns;
      v["tilecount"] = columns * rows;
    });
  }
  ui::tooltip("Tile width and height, in pixels");
  ImGui::SameLine(0, 4);
  if (ui::iconButton("sheet", ICON_SQUARES_FOUR, sheet ? "Change the sheet image" : "Cut tiles from one sheet image instead")) {
    ImGui::OpenPopup("sheetImage");
  }
  std::string sheetImage;
  if (widgets::imagePopup("sheetImage", project, sheetImage)) {
    if (auto picture = Thumbnails::instance().get(project, sheetImage)) {
      doc.edit("Use Sheet " + fs::path(sheetImage).filename().string(), [&](Json& v) {
        const int w = static_cast<int>(picture->size.x), h = static_cast<int>(picture->size.y);
        const int tw = std::max(1, v.value("tilewidth", 16)), th = std::max(1, v.value("tileheight", 16));
        v["image"] = tiled::relativeTo(doc.path(), sheetImage);
        v["imagewidth"] = w;
        v["imageheight"] = h;
        v["columns"] = std::max(1, w / tw);
        v["tilecount"] = std::max(1, w / tw) * std::max(1, h / th);
        // Per-tile images belong to collections; a sheet keeps only the tiles' properties.
        if (v.contains("tiles")) {
          for (Json& t : v["tiles"]) t.erase("image"), t.erase("imagewidth"), t.erase("imageheight");
        }
      });
    }
  }
  ImGui::SameLine(0, 4);
  if (ui::iconButton("tiled", ICON_ARROW_SQUARE_OUT, "Open in Tiled")) editor.openInTiled(doc.path());
  ui::endDocumentBar();

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16, 10});
  ImGui::BeginChild("##tiles", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
  drawTerrainBar(doc);
  ImGui::Dummy({0, 6});
  if (tiled::tileIds(value).empty() && !sheet) {
    ui::smallText("No tiles yet: add images (each one a tile), or cut a sheet image into tiles with " ICON_SQUARES_FOUR " above.", theme::textFaint);
    ImGui::Dummy({0, 6});
  }
  drawCards(editor, doc);
  ImGui::Dummy({0, 10});
  ui::smallText(ICON_PROHIBIT " solid    " ICON_FILM_STRIP " animated    Select a tile to set its type and behavior.", theme::textFaint);
  ImGui::EndChild();
  ImGui::PopStyleVar();
}

void TilesetEditor::terrainInspector(AssetDocument& doc) {
  const Json ws = doc.value()["wangsets"][static_cast<size_t>(_terrain)];
  const int set = _terrain;
  auto editSet = [&](const std::string& label, const std::function<void(Json&)>& change, const std::string& key = {}) {
    doc.edit(label, [&](Json& v) { change(v["wangsets"][static_cast<size_t>(set)]); }, key);
  };
  ui::heading(ws.value("name", std::string("Terrain")).c_str());
  ui::smallText("Tiles whose edges or corners show this terrain. The scene's terrain brush picks the tile that fits its neighbours.", theme::textFaint);
  ImGui::Dummy({0, 6});
  if (!ui::beginProperties("terrain", 90)) return;
  ui::propertyRow("Name");
  std::string name = ws.value("name", std::string());
  if (ImGui::InputText("##name", &name)) editSet("Rename Terrain", [&](Json& w) { w["name"] = name; }, "terrainName");
  ui::propertyRow("Joins by", "Edges: a tile joins its neighbours side to side (paths, fences). Corners: tiles share corners (ground, water). Mixed: both.");
  const std::string type = ws.value("type", std::string("corner"));
  if (ui::beginCombo("##type", type.c_str())) {
    for (const char* t : kTerrainTypes) {
      if (ImGui::Selectable(t, type == t)) editSet("Set Terrain Type", [&](Json& w) { w["type"] = t; });
    }
    ImGui::EndCombo();
  }
  ui::endProperties();
  ImGui::Dummy({0, 6});
  ui::sectionLabel("Colors");
  const Json colors = ws.value("colors", Json::array());
  for (size_t c = 0; c < colors.size(); ++c) {
    ImGui::PushID(static_cast<int>(c));
    const ImVec4 rgb = hexColor(colors[c].value("color", std::string()));
    float color[3] = {rgb.x, rgb.y, rgb.z};
    if (ImGui::ColorEdit3("##color", color, ImGuiColorEditFlags_NoInputs)) {
      editSet("Set Terrain Color", [&](Json& w) { w["colors"][c]["color"] = colorHex(color); }, "terrainColor" + std::to_string(c));
    }
    ImGui::SameLine(0, 6);
    std::string colorName = colors[c].value("name", std::string());
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - (colors.size() > 1 ? ImGui::GetFrameHeight() + 6 : 0));
    if (ImGui::InputText("##colorName", &colorName)) editSet("Rename Color", [&](Json& w) { w["colors"][c]["name"] = colorName; }, "colorName" + std::to_string(c));
    if (colors.size() > 1) ImGui::SameLine(0, 4);
    if (colors.size() > 1 && ui::iconButton("remove", ICON_TRASH, "Remove this color (tiles lose it)")) {
      const int removed = static_cast<int>(c) + 1;
      editSet("Remove Color", [&](Json& w) {
        w["colors"].erase(c);
        for (Json& t : w["wangtiles"]) {
          for (Json& k : t["wangid"]) k = k.get<int>() == removed ? 0 : k.get<int>() > removed ? k.get<int>() - 1 : k.get<int>();
        }
      });
      _color = 1;
    }
    ImGui::PopID();
  }
  if (ui::button(ICON_PLUS "  Color")) {
    editSet("Add Color", [&](Json& w) {
      w["colors"].push_back({{"name", "Color " + std::to_string(w["colors"].size() + 1)}, {"color", kPalette[w["colors"].size() % 8]}, {"probability", 1}, {"tile", -1}});
    });
  }
  ImGui::Dummy({0, 10});
  if (ui::dangerButton(ICON_TRASH "  Delete Terrain", {-FLT_MIN, 0})) {
    doc.edit("Delete Terrain", [&](Json& v) {
      v["wangsets"].erase(static_cast<size_t>(set));
      if (v["wangsets"].empty()) v.erase("wangsets");
    });
    _terrain = -1;
  }
}

void TilesetEditor::tileInspector(Editor& editor, AssetDocument& doc) {
  const Project& project = *editor.project();
  const Json& value = doc.value();
  const uint32_t id = _selected;
  const Json entry = tiled::tileEntry(value, id) ? *tiled::tileEntry(value, id) : Json{{"id", id}};
  const std::string type = entry.value("type", entry.value("class", std::string()));

  // The tile, big.
  const float width = ImGui::GetContentRegionAvail().x, h = 120.0f;
  const ImVec2 a = ImGui::GetCursorScreenPos();
  ImGui::Dummy({width, h});
  widgets::checker(ImGui::GetWindowDrawList(), a, {a.x + width, a.y + h});
  if (auto p = widgets::tilePicture(project, value, doc.path(), id)) {
    widgets::fitted(ImGui::GetWindowDrawList(), *p, {a.x + 10, a.y + 10}, {a.x + width - 10, a.y + h - 10});
  }
  const auto placed = _placed.find(id);
  ui::smallText(placed == _placed.end() ? "Not placed in any map yet" : ("Placed " + std::to_string(placed->second) + " times").c_str(), theme::textFaint);
  // Painting with it: in a scene showing a map that uses this tileset.
  std::string scene;
  for (const std::string& map : _maps) {
    if (auto scenes = editor.scenesUsingMap(map); !scenes.empty()) {
      scene = scenes.front();
      break;
    }
  }
  if (!scene.empty() && ui::primaryButton(ICON_PAINT_BRUSH "  Paint with This Tile", {-FLT_MIN, 0})) {
    const std::string tileset = doc.path();
    editor.openSceneAt(scene, [&](const Json& c) {
      const Json map = c.value("TileMapComponent", Json::object()).value("map", Json());
      return std::find(_maps.begin(), _maps.end(), map.is_string() ? map.get<std::string>() : "") != _maps.end();
    });
    editor.tileBrush() = Editor::TileBrush{tileset, id};
    editor.setTool(Tool::TileBrush);
    editor.focusPanel("Scene");
  }
  ImGui::Dummy({0, 4});

  if (!ui::beginProperties("tile", 96)) return;
  ui::propertyRow("Type", "What scripts call it: map.at(x, y) == \"type\"; map.set(x, y, \"type\")");
  std::string typed = type;
  if (ImGui::InputTextWithHint("##type", "untyped", &typed)) {
    editTile(doc, "Set Tile Type", [&](Json& t) {
      t.erase("class");
      if (typed.empty()) t.erase("type");
      else t["type"] = typed;
    }, "type");
  }
  ui::propertyRow("Solid", "Blocks TileBody movement. On an upper layer, an explicit off lets bodies cross what's below (a bridge over water).");
  const Json solid = tiled::property(entry, "solid");
  const char* solidLabel = solid.is_null() ? "Not set" : solid == Json(true) ? "Solid" : "Passable (over solid)";
  if (ui::beginCombo("##solid", solidLabel)) {
    if (ImGui::Selectable("Not set", solid.is_null())) editTile(doc, "Unset Solid", [](Json& t) { tiled::setProperty(t, "solid", nullptr); });
    if (ImGui::Selectable("Solid", solid == Json(true))) editTile(doc, "Make Solid", [](Json& t) { tiled::setProperty(t, "solid", true); });
    if (ImGui::Selectable("Passable (over solid)", solid == Json(false))) editTile(doc, "Make Passable", [](Json& t) { tiled::setProperty(t, "solid", false); });
    ImGui::EndCombo();
  }
  if (value.value("image", std::string()).empty()) {
    ui::propertyRow("Image");
    const std::string image = entry.value("image", std::string());
    if (ui::button((ui::ellipsize(fs::path(image).filename().string(), ImGui::GetContentRegionAvail().x - 16)).c_str(), {-FLT_MIN, 0})) {
      ImGui::OpenPopup("tileImage");
    }
    std::string picked;
    if (widgets::imagePopup("tileImage", project, picked)) {
      const auto picture = Thumbnails::instance().get(project, picked);
      editTile(doc, "Set Tile Image", [&](Json& t) {
        t["image"] = tiled::relativeTo(doc.path(), picked);
        if (picture) t["imagewidth"] = static_cast<int>(picture->size.x), t["imageheight"] = static_cast<int>(picture->size.y);
        t.erase("x"), t.erase("y"), t.erase("width"), t.erase("height");
      });
    }
  }
  ui::endProperties();

  // Custom properties: bools are tags (map.is(x, y, "deadly")); scripts read the rest.
  ImGui::Dummy({0, 6});
  ui::sectionLabel("Properties");
  widgets::properties(entry, _newProperty, [&](const std::string& label, const std::function<void(Json&)>& change, const std::string& key) {
    editTile(doc, label, change, key);
  }, {"solid"});
  ui::smallText("A bool property is a tag: map.is(x, y, \"name\").", theme::textFaint);

  // Animation: frames of this tileset's tiles.
  ImGui::Dummy({0, 6});
  ui::sectionLabel("Animation");
  const Json frames = entry.value("animation", Json::array());
  for (size_t i = 0; i < frames.size(); ++i) {
    ImGui::PushID(static_cast<int>(i) + 1000);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float s = ImGui::GetFrameHeight();
    ImGui::Dummy({s, s});
    if (auto pic = widgets::tilePicture(project, value, doc.path(), frames[i].value("tileid", 0u))) {
      widgets::fitted(ImGui::GetWindowDrawList(), *pic, p, {p.x + s, p.y + s});
    }
    ImGui::SameLine(0, 6);
    int ms = frames[i].value("duration", 100);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - s - 4);
    if (ImGui::DragInt("##ms", &ms, 1.0f, 1, 60000, "%d ms")) {
      editTile(doc, "Set Frame Time", [&](Json& t) { t["animation"][i]["duration"] = std::max(1, ms); }, "frame" + std::to_string(i));
    }
    ImGui::SameLine(0, 4);
    if (ui::iconButton("remove", ICON_X, "Remove frame")) {
      editTile(doc, "Remove Frame", [&](Json& t) {
        t["animation"].erase(i);
        if (t["animation"].empty()) t.erase("animation");
      });
    }
    ImGui::PopID();
  }
  if (ui::button(ICON_PLUS "  Frame")) ImGui::OpenPopup("frameTile");
  if (frames.empty()) ui::smallText("Frames are tiles of this tileset, shown in turn.", theme::textFaint);
  if (ImGui::BeginPopup("frameTile")) {
    const float cell = 44.0f;
    int n = 0;
    for (uint32_t other : tiled::tileIds(value)) {
      if (n++ % 6) ImGui::SameLine(0, 4);
      ImGui::PushID(static_cast<int>(other));
      const ImVec2 p = ImGui::GetCursorScreenPos();
      if (ImGui::InvisibleButton("##f", {cell, cell})) {
        editTile(doc, "Add Frame", [&](Json& t) {
          if (!t.contains("animation")) t["animation"] = Json::array({{{"tileid", id}, {"duration", 200}}});
          t["animation"].push_back({{"tileid", other}, {"duration", 200}});
        });
        ImGui::CloseCurrentPopup();
      }
      ImGui::GetWindowDrawList()->AddRectFilled(p, {p.x + cell, p.y + cell}, theme::u32(ImGui::IsItemHovered() ? theme::bg4 : theme::bg2), theme::radius);
      if (auto pic = widgets::tilePicture(project, value, doc.path(), other)) {
        widgets::fitted(ImGui::GetWindowDrawList(), *pic, {p.x + 4, p.y + 4}, {p.x + cell - 4, p.y + cell - 4});
      }
      ImGui::PopID();
    }
    ImGui::EndPopup();
  }
}

bool TilesetEditor::drawInspector(Editor& editor, AssetDocument& doc) {
  if (_terrain >= 0 && _terrain < static_cast<int>(doc.value().value("wangsets", Json::array()).size())) {
    terrainInspector(doc);
    return true;
  }
  const auto ids = tiled::tileIds(doc.value());
  if (!_hasSelection || std::find(ids.begin(), ids.end(), _selected) == ids.end()) {
    ui::emptyState(ICON_GRID_FOUR, "Pick a tile", "Select a tile to give it a type scripts know it by, make it solid, tag it or animate it.");
    return true;
  }
  tileInspector(editor, doc);
  return true;
}

}  // namespace

std::unique_ptr<AssetEditor> makeTilesetEditor() { return std::make_unique<TilesetEditor>(); }
