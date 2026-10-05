// The tileset editor (*.tileset.json): every tile character as a card, and the
// selected one's look (image, animation, edge-aware variants with a live
// terrain preview) and behavior (solid, tags, what's drawn under it), with how
// often the maps that use this tileset place it.

#include <cmath>
#include <map>
#include <set>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "AssetEditor.hpp"
#include "Editor.hpp"
#include "EditorWidgets.hpp"
#include "Icons.hpp"
#include "Theme.hpp"
#include "Ui.hpp"

namespace {

// A map drawn with this tileset, somewhere in the project.
struct MapUse {
  std::string where;  // the scene or prefab placing it
  std::string rows;   // its map file, or "" when the rows are inline
  Json vars = Json::object();
  std::map<char, size_t> counts;  // tiles placed, by character
};

// Finds the maps drawn with `tileset`: TileMapComponents in scenes and
// prefabs (with prefab instances' overrides merged in).
std::vector<MapUse> findUses(const Project& project, const std::string& tileset) {
  std::vector<MapUse> out;
  std::set<std::string> seen;  // one entry per map file
  auto consider = [&](const std::string& where, Json tilemap) {
    if (tilemap.value("tileset", std::string()) != tileset) return;
    MapUse use{where, "", tilemap.value("vars", Json::object()), {}};
    std::vector<std::string> rows;
    if (tilemap.contains("rows") && tilemap["rows"].is_string()) {
      use.rows = tilemap["rows"];
      if (!seen.insert(use.rows + use.vars.dump()).second) return;
      std::string text = project.readText(use.rows);
      for (char c : text) {
        if (c != '\n' && c != '\r') ++use.counts[c];
      }
    } else if (tilemap.contains("rows") && tilemap["rows"].is_array()) {
      for (const Json& r : tilemap["rows"]) {
        if (r.is_string()) {
          for (char c : r.get<std::string>()) ++use.counts[c];
        }
      }
    }
    out.push_back(std::move(use));
  };
  for (const AssetFile& f : project.files()) {
    if (f.kind != AssetKind::Scene && f.kind != AssetKind::Prefab) continue;
    const Json doc = Json::parse(project.readText(f.path), nullptr, false);
    if (doc.is_discarded()) continue;
    if (f.kind == AssetKind::Prefab) {
      if (doc.contains("components") && doc["components"].contains("TileMapComponent")) {
        consider(f.path, doc["components"]["TileMapComponent"]);
      }
      continue;
    }
    for (const Json& e : doc.value("entities", Json::array())) {
      Json tilemap;
      if (e.contains("prefab")) {
        const Json prefab = Json::parse(project.readText(e["prefab"].get<std::string>()), nullptr, false);
        if (!prefab.is_discarded() && prefab.contains("components")) tilemap = prefab["components"].value("TileMapComponent", Json());
        if (e.contains("overrides") && e["overrides"].contains("TileMapComponent")) {
          if (!tilemap.is_object()) tilemap = Json::object();
          tilemap.merge_patch(e["overrides"]["TileMapComponent"]);
        }
      } else if (e.contains("components")) {
        tilemap = e["components"].value("TileMapComponent", Json());
      }
      if (tilemap.is_object()) consider(f.path, tilemap);
    }
  }
  return out;
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
  for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
  return s;
}

// What a tile draws at an edge mask and animation frame, with the map's vars:
// a region name, or a full reference ("assets/x.png", "a.atlas.json#r").
// A negative mask or frame keeps that placeholder ("path_{mask}", for labels).
std::string imageOf(const Json& tile, const Json& vars, int mask, int frame) {
  const Json image = tile.value("image", Json());
  std::string name;
  if (image.is_array() && !image.empty()) name = image[static_cast<size_t>(std::max(frame, 0)) % image.size()].get<std::string>();
  else if (image.is_string()) name = image;
  if (name.empty()) return "";
  for (const auto& [k, v] : vars.items()) {
    if (v.is_string()) name = replaceAll(name, "{" + k + "}", v.get<std::string>());
  }
  if (mask >= 0) name = replaceAll(name, "{mask}", std::to_string(mask));
  return frame >= 0 ? replaceAll(name, "{frame}", std::to_string(frame)) : name;
}

// The frame showing now, for an animated tile.
int frameNow(const Json& tile) {
  const Json image = tile.value("image", Json());
  const int frames = image.is_array() ? static_cast<int>(image.size())
                     : image.is_string() && image.get<std::string>().find("{frame}") != std::string::npos ? tile.value("frames", 1)
                                                                                                        : 1;
  const float duration = std::max(0.05f, tile.value("frameDuration", 0.25f));
  return frames > 1 ? static_cast<int>(ImGui::GetTime() / duration) % frames : 0;
}

// "theme: over_": how a map's vars read in the look menu.
std::string lookLabel(const Json& vars) {
  std::string label;
  for (const auto& [k, v] : vars.items()) label += (label.empty() ? "" : ", ") + k + ": " + (v.is_string() ? v.get<std::string>() : v.dump());
  return label;
}

bool usesMask(const Json& tile) {
  const Json image = tile.value("image", Json());
  return (image.is_string() && image.get<std::string>().find("{mask}") != std::string::npos) || tile.contains("edges");
}

// Sides as edge-mask bits: 1 north, 2 east, 4 south, 8 west.
constexpr const char* kSides[] = {"N", "E", "S", "W"};

int sidesMask(const std::string& sides) {
  int mask = 0;
  for (int i = 0; i < 4; ++i) {
    if (sides.find(kSides[i]) != std::string::npos) mask |= 1 << i;
  }
  return mask;
}

std::string maskSides(int mask) {
  std::string out;
  for (int i = 0; i < 4; ++i) {
    if (mask & (1 << i)) out += kSides[i];
  }
  return out;
}

// The image an edge-aware tile shows where `open` sides border other terrain.
std::string imageAt(const Json& tile, const Json& vars, int open, int frame) {
  for (const Json& rule : tile.value("edges", Json::array())) {
    const int need = sidesMask(rule.value("open", std::string()));
    const int shut = sidesMask(rule.value("closed", std::string()));
    if ((open & need) == need && (open & shut) == 0) {
      Json ruleTile = tile;
      ruleTile["image"] = rule.value("image", std::string());
      return imageOf(ruleTile, vars, open, frame);
    }
  }
  return imageOf(tile, vars, open, frame);
}

class TilesetEditor final : public AssetEditor {
 public:
  void draw(Editor& editor, AssetDocument& doc) override;
  bool drawInspector(Editor& editor, AssetDocument& doc) override;
  bool handles(const std::string& command) const override {
    return !_selected.empty() && (command == "edit.duplicate" || command == "edit.delete");
  }
  void run(const std::string& command, AssetDocument& doc) override {
    const std::string key = _selected;
    if (!doc.value().value("tiles", Json::object()).contains(key)) return;
    if (command == "edit.delete") {
      doc.edit("Delete Tile " + key, [&](Json& v) { v["tiles"].erase(key); });
      _selected.clear();
      return;
    }
    doc.edit("Duplicate Tile " + key, [&](Json& v) {
      for (char c = '!'; c <= '~'; ++c) {
        if (!v["tiles"].contains(std::string(1, c))) {
          v["tiles"][std::string(1, c)] = v["tiles"][key];
          _selected = std::string(1, c);
          return;
        }
      }
    });
  }

 private:
  std::string _selected;  // tile character (as a string: JSON keys are strings)
  std::string _filter;
  int _look = 0;  // which map's vars preview {name} images
  std::string _newTag;
  std::string _charDraft, _charDraftFor;
  std::vector<MapUse> _uses;
  size_t _usesSignature = 1;

  std::optional<Thumbnails::Picture> picture(const Project& project, const std::string& atlas, const std::string& image) const;
  const Json& vars() const;
  void drawGrid(const Project& project, AssetDocument& doc, const Json& tiles, const std::string& atlas);
  void drawDetail(Editor& editor, AssetDocument& doc, const Json& tiles, const std::string& atlas);
  void drawPreview(const Project& project, const Json& tile, const std::string& atlas);
  void editTile(AssetDocument& doc, const std::string& label, const std::function<void(Json&)>& mutate,
                const std::string& mergeKey = {});
};

const Json& TilesetEditor::vars() const {
  static const Json none = Json::object();
  return _uses.empty() ? none : _uses[static_cast<size_t>(_look) % _uses.size()].vars;
}

std::optional<Thumbnails::Picture> TilesetEditor::picture(const Project& project, const std::string& atlas,
                                                          const std::string& image) const {
  if (image.empty()) return std::nullopt;
  const bool full = image.find('#') != std::string::npos || image.ends_with(".png");
  return Thumbnails::instance().get(project, full ? image : atlas + "#" + image);
}

void TilesetEditor::editTile(AssetDocument& doc, const std::string& label, const std::function<void(Json&)>& mutate,
                             const std::string& mergeKey) {
  const std::string c = _selected;
  doc.edit(label + " (" + c + ")", [&](Json& v) { mutate(v["tiles"][c]); }, mergeKey);
}

void TilesetEditor::drawPreview(const Project& project, const Json& tile, const std::string& atlas) {
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const float width = ImGui::GetContentRegionAvail().x;
  const int frame = frameNow(tile);
  if (!usesMask(tile)) {
    const float h = 150.0f;
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImGui::Dummy({width, h});
    widgets::checker(draw, a, {a.x + width, a.y + h});
    if (auto p = picture(project, atlas, imageOf(tile, vars(), 0, frame))) {
      widgets::fitted(draw, *p, {a.x + 12, a.y + 12}, {a.x + width - 12, a.y + h - 12});
    } else {
      const char* text = tile.contains("image") ? "Image not found in the atlas" : "No image: an invisible tile";
      const ImVec2 ts = ImGui::CalcTextSize(text);
      draw->AddText({a.x + (width - ts.x) * 0.5f, a.y + (h - ts.y) * 0.5f}, theme::u32(theme::textDim), text);
    }
    return;
  }
  // Edge-aware: a small patch of this terrain, each cell picking its variant
  // by which sides border something else, like the map will.
  static constexpr const char* kPatch[] = {"........", ".####...", ".######.", ".##..##.", ".######.", "...##...", "........"};
  const int rows = 7, cols = 8;
  const float cell = std::floor(std::min(width / cols, 26.0f));
  const ImVec2 a = ImGui::GetCursorScreenPos();
  const ImVec2 size{cell * cols, cell * rows};
  const float x0 = a.x + std::floor((width - size.x) * 0.5f);
  ImGui::Dummy({width, size.y});
  widgets::checker(draw, {x0, a.y}, {x0 + size.x, a.y + size.y}, cell * 0.5f);
  auto inside = [&](int r, int c) { return r >= 0 && r < rows && c >= 0 && c < cols && kPatch[r][c] == '#'; };
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      if (!inside(r, c)) continue;
      const int open = (!inside(r - 1, c) ? 1 : 0) | (!inside(r, c + 1) ? 2 : 0) | (!inside(r + 1, c) ? 4 : 0) | (!inside(r, c - 1) ? 8 : 0);
      if (auto p = picture(project, atlas, imageAt(tile, vars(), open, frame))) {
        const ImVec2 p0{x0 + c * cell, a.y + r * cell};
        draw->AddImage(p->texture, p0, {p0.x + cell, p0.y + cell}, p->uv0, p->uv1);
      }
    }
  }
  ui::smallText("Preview: this terrain as the map draws it, edges and all.", theme::textFaint);
}

void TilesetEditor::drawGrid(const Project& project, AssetDocument& doc, const Json& tiles, const std::string& atlas) {
  const float card = 92.0f;
  const int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + 10) / (card + 10)));
  int shown = 0;
  for (const auto& [key, tile] : tiles.items()) {
    if (!_filter.empty() && key != _filter && ui::fuzzyScore(tile.value("image", Json("")).is_string() ? tile.value("image", std::string()) : "", _filter) < 0) continue;
    if (shown++ % columns) ImGui::SameLine(0, 10);
    ImGui::PushID(key.c_str());
    const ImVec2 a = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##tile", {card, card + 22})) _selected = key;
    const bool hovered = ImGui::IsItemHovered();
    const bool selected = key == _selected;
    if (ImGui::BeginPopupContextItem("tileMenu")) {
      _selected = key;
      if (ImGui::MenuItem(ICON_COPY "  Duplicate")) {
        doc.edit("Duplicate Tile " + key, [&](Json& v) {
          for (char c = '!'; c <= '~'; ++c) {
            const std::string k(1, c);
            if (!v["tiles"].contains(k)) {
              v["tiles"][k] = tile;
              _selected = k;
              break;
            }
          }
        });
      }
      if (ImGui::MenuItem(ICON_TRASH "  Delete")) doc.edit("Delete Tile " + key, [&](Json& v) { v["tiles"].erase(key); });
      ImGui::EndPopup();
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(a, {a.x + card, a.y + card + 22}, theme::u32(selected ? theme::bg3 : hovered ? theme::bg2 : theme::bg1), theme::radiusOverlay);
    widgets::checker(draw, {a.x + 6, a.y + 6}, {a.x + card - 6, a.y + card - 6}, 6.0f);
    const std::string image = imageOf(tile, vars(), 0, frameNow(tile));
    auto pic = picture(project, atlas, image);
    if (!pic && tile.contains("under")) {  // a marker drawn over what's under it
      const std::string under = tile["under"];
      if (!under.empty() && tiles.contains(std::string(1, under.back()))) {
        pic = picture(project, atlas, imageOf(tiles[std::string(1, under.back())], vars(), 0, 0));
      }
      if (pic) widgets::fitted(draw, *pic, {a.x + 10, a.y + 10}, {a.x + card - 10, a.y + card - 10}, 0.35f);
      pic.reset();
    }
    if (pic) widgets::fitted(draw, *pic, {a.x + 10, a.y + 10}, {a.x + card - 10, a.y + card - 10});
    // The character, big, in a corner badge; behavior icons in the other.
    ImGui::PushFont(theme::fonts().mono, 15.0f);
    const ImVec2 ks = ImGui::CalcTextSize(key.c_str());
    draw->AddRectFilled({a.x + 4, a.y + 4}, {a.x + ks.x + 14, a.y + ks.y + 8}, theme::u32(theme::bg0, 0.85f), theme::radius);
    draw->AddText({a.x + 9, a.y + 6}, theme::u32(selected ? theme::accentBright : theme::text), key.c_str());
    ImGui::PopFont();
    std::string flags;
    if (tile.value("solid", false)) flags += ICON_PROHIBIT;
    if (usesMask(tile)) flags += ICON_GRID_NINE;
    if (frameNow(tile) || tile.value("image", Json()).is_array() || tile.contains("frames")) flags += ICON_FILM_STRIP;
    if (!flags.empty()) {
      const ImVec2 fs = ImGui::CalcTextSize(flags.c_str());
      draw->AddRectFilled({a.x + card - fs.x - 14, a.y + 4}, {a.x + card - 4, a.y + fs.y + 8}, theme::u32(theme::bg0, 0.85f), theme::radius);
      draw->AddText({a.x + card - fs.x - 9, a.y + 6}, theme::u32(theme::textDim), flags.c_str());
    }
    // Name under the card.
    std::string label = image.empty() ? (tile.contains("under") ? "marker" : "empty") : imageOf(tile, vars(), -1, -1);
    ImGui::PushFont(nullptr, theme::sizeSmall);
    label = ui::ellipsize(label, card - 8);
    const ImVec2 ls = ImGui::CalcTextSize(label.c_str());
    draw->AddText({a.x + (card - ls.x) * 0.5f, a.y + card + 2}, theme::u32(selected ? theme::text : theme::textDim), label.c_str());
    ImGui::PopFont();
    if (selected) draw->AddRect(a, {a.x + card, a.y + card + 22}, theme::u32(theme::accent), theme::radiusOverlay, 2.0f);
    ImGui::PopID();
  }
  // The add card.
  if (shown % columns) ImGui::SameLine(0, 10);
  const ImVec2 a = ImGui::GetCursorScreenPos();
  if (ImGui::InvisibleButton("##add", {card, card + 22})) {
    doc.edit("Add Tile", [&](Json& v) {
      for (const char* pool : {"abcdefghijklmnopqrstuvwxyz", "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", "!$%&*+-:;<=>?@^_|~"}) {
        for (const char* c = pool; *c; ++c) {
          const std::string k(1, *c);
          if (!v["tiles"].contains(k)) {
            v["tiles"][k] = Json::object();
            _selected = k;
            return;
          }
        }
      }
    });
  }
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRect(a, {a.x + card, a.y + card + 22}, theme::u32(hovered ? theme::accent : theme::border), theme::radiusOverlay, 1.5f);
  ImGui::PushFont(nullptr, 22.0f);
  const ImVec2 ps = ImGui::CalcTextSize(ICON_PLUS);
  draw->AddText({a.x + (card - ps.x) * 0.5f, a.y + (card - ps.y) * 0.5f}, theme::u32(hovered ? theme::accent : theme::textDim), ICON_PLUS);
  ImGui::PopFont();
  ImGui::PushFont(nullptr, theme::sizeSmall);
  const ImVec2 ts = ImGui::CalcTextSize("Add Tile");
  draw->AddText({a.x + (card - ts.x) * 0.5f, a.y + card + 2}, theme::u32(theme::textDim), "Add Tile");
  ImGui::PopFont();
}

void TilesetEditor::drawDetail(Editor& editor, AssetDocument& doc, const Json& tiles, const std::string& atlas) {
  const Project& project = *editor.project();
  if (_selected.empty() || !tiles.contains(_selected)) {
    ui::emptyState(ICON_GRID_FOUR, "Pick a tile", "Each tile is a character the maps are written in. Select one to change its look and behavior.");
    return;
  }
  const Json tile = tiles[_selected];

  // Character: the big glyph, editable.
  ImGui::PushFont(theme::fonts().mono, 34.0f);
  ImGui::TextColored(theme::accent, "%s", _selected.c_str());
  ImGui::PopFont();
  ImGui::SameLine(0, 14);
  ImGui::BeginGroup();
  ImGui::PushFont(theme::fonts().semibold, theme::sizeTitle);
  ImGui::TextUnformatted(tile.contains("image") ? "Tile" : tile.contains("under") ? "Marker tile" : "Empty tile");
  ImGui::PopFont();
  size_t placed = 0;
  std::string where;
  for (const MapUse& use : _uses) {
    if (auto it = use.counts.find(_selected[0]); it != use.counts.end() && it->second) {
      placed += it->second;
      where += (where.empty() ? "" : ", ") + std::filesystem::path(use.rows.empty() ? use.where : use.rows).filename().string();
    }
  }
  ui::smallText(placed ? ("Placed " + std::to_string(placed) + " times in " + where).c_str() : "Not placed in any map yet", theme::textFaint);
  ImGui::EndGroup();
  ImGui::Dummy({0, 6});

  drawPreview(project, tile, atlas);
  ImGui::Dummy({0, 6});
  // Straight to painting: the scene drawing a map with this tileset, this tile in the brush.
  std::string paintIn;
  size_t most = 0;
  for (const MapUse& use : _uses) {
    if (assetKindOf(use.where) != AssetKind::Scene) continue;
    const auto it = use.counts.find(_selected[0]);
    const size_t placed = it == use.counts.end() ? 0 : it->second;
    if (paintIn.empty() || placed > most) paintIn = use.where, most = placed;  // where it's used most
  }
  if (!paintIn.empty() && ui::button((std::string(ICON_PAINT_BRUSH "  Paint with ") + _selected).c_str(), {-FLT_MIN, 0})) {
    editor.openSceneAt(paintIn, [&](const Json& c) {
      return c.value("TileMapComponent", Json::object()).value("tileset", std::string()) == doc.path();
    });
    editor.setBrushTile(_selected[0]);
    editor.setTool(Tool::TileBrush);
    editor.focusPanel("Scene");
  }
  ImGui::Dummy({0, 4});

  if (!ui::beginProperties("tile", 110)) return;
  ui::propertyRow("Character", "The character maps use for this tile");
  if (_charDraftFor != _selected) _charDraft = _charDraftFor = _selected;
  ImGui::SetNextItemWidth(60);
  if (ImGui::InputText("##char", &_charDraft, ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_AutoSelectAll) &&
      _charDraft.size() == 1 && _charDraft != _selected && !tiles.contains(_charDraft)) {
    const std::string from = _selected, to = _charDraft;
    doc.edit("Change Tile " + from + " to " + to, [&](Json& v) {
      Json renamed = Json::object();
      for (auto& [k, t] : v["tiles"].items()) renamed[k == from ? to : k] = t;
      v["tiles"] = renamed;
    });
    _selected = _charDraftFor = to;
  }
  if (placed && _charDraft == _selected) {
    ImGui::SameLine();
    ImGui::TextColored(theme::textFaint, ICON_INFO);
    ui::tooltip("Changing it doesn't rewrite the maps: placed tiles keep their old character.");
  }

  // Look.
  const Json image = tile.value("image", Json());
  ui::propertyRow(image.is_array() ? "Frames" : "Image", "An atlas region. {mask} picks an edge variant (0-15), {frame} an animation frame, {name} a map var.");
  if (image.is_array()) {
    for (size_t i = 0; i < image.size(); ++i) {
      ImGui::PushID(static_cast<int>(i));
      std::string frame = image[i].is_string() ? image[i].get<std::string>() : "";
      const float w = ImGui::GetContentRegionAvail().x;
      ImGui::SetNextItemWidth(w - ImGui::GetFrameHeight() - 4);
      ImGui::BeginGroup();
      ImGui::PushItemWidth(w - ImGui::GetFrameHeight() - 4);
      if (widgets::regionField("f", project, atlas, frame)) editTile(doc, "Edit Frame", [&](Json& t) { t["image"][i] = frame; }, "frame" + std::to_string(i));
      ImGui::PopItemWidth();
      ImGui::EndGroup();
      ImGui::SameLine(0, 4);
      if (ui::iconButton("x", ICON_X, "Remove frame")) {
        editTile(doc, "Remove Frame", [&](Json& t) {
          t["image"].erase(i);
          if (t["image"].size() == 1) t["image"] = t["image"][0];
        });
      }
      ImGui::PopID();
    }
    if (ui::button(ICON_PLUS "  Frame")) editTile(doc, "Add Frame", [&](Json& t) { t["image"].push_back(t["image"].back()); });
  } else {
    std::string value = image.is_string() ? image.get<std::string>() : "";
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::BeginGroup();
    ImGui::PushItemWidth(w - ImGui::GetFrameHeight() - 4);
    if (widgets::regionField("image", project, atlas, value, "none (invisible)")) {
      editTile(doc, "Set Image", [&](Json& t) {
        if (value.empty()) t.erase("image");
        else t["image"] = value;
      }, "image");
    }
    ImGui::PopItemWidth();
    ImGui::EndGroup();
    ImGui::SameLine(0, 4);
    if (ui::iconButton("animate", ICON_FILM_STRIP, "Animate with a list of frames") && !value.empty()) {
      editTile(doc, "Animate", [&](Json& t) { t["image"] = Json::array({value, value}); });
    }
  }
  const bool frameVar = image.is_string() && image.get<std::string>().find("{frame}") != std::string::npos;
  if (frameVar) {
    ui::propertyRow("Frame count", "How many {frame} images there are (0, 1, 2...)");
    int frames = tile.value("frames", 1);
    if (ImGui::DragInt("##frames", &frames, 0.1f, 1, 64)) editTile(doc, "Set Frames", [&](Json& t) { t["frames"] = frames; }, "frames");
  }
  if (frameVar || image.is_array()) {
    ui::propertyRow("Frame time", "Seconds each frame shows");
    float d = tile.value("frameDuration", 0.25f);
    if (ImGui::DragFloat("##duration", &d, 0.01f, 0.02f, 10.0f, "%.2f s")) {
      editTile(doc, "Set Frame Time", [&](Json& t) { t["frameDuration"] = std::round(d * 100.0f) / 100.0f; }, "duration");
    }
  }
  ui::propertyRow("Anchor", "Images larger than a cell grow from its bottom edge (center) or its bottom-left corner");
  const bool bottomLeft = tile.value("anchor", std::string()) == "bottom-left";
  if (ui::beginCombo("##anchor", bottomLeft ? "Bottom-left" : "Center (default)")) {
    if (ImGui::Selectable("Center (default)", !bottomLeft)) editTile(doc, "Set Anchor", [](Json& t) { t.erase("anchor"); });
    if (ImGui::Selectable("Bottom-left", bottomLeft)) editTile(doc, "Set Anchor", [](Json& t) { t["anchor"] = "bottom-left"; });
    ImGui::EndCombo();
  }

  // Behavior.
  ui::propertyRow("Solid", "Blocks TileBody movement (walls, ground)");
  bool solid = tile.value("solid", false);
  if (ui::toggle("##solid", &solid)) {
    editTile(doc, solid ? "Make Solid" : "Make Passable", [&](Json& t) {
      if (solid) t["solid"] = true;
      else t.erase("solid");
    });
  }
  ui::propertyRow("Tags", "Scripts ask map.is(tx, ty, \"tag\")");
  const Json tags = tile.value("tags", Json::array());
  for (size_t i = 0; i < tags.size(); ++i) {
    bool removed = false;
    ImGui::PushID(static_cast<int>(i));
    ui::chip("tag", tags[i].get<std::string>().c_str(), true, &removed);
    ImGui::PopID();
    if (removed) {
      editTile(doc, "Remove Tag", [&](Json& t) {
        t["tags"].erase(i);
        if (t["tags"].empty()) t.erase("tags");
      });
    }
    ImGui::SameLine(0, 4);
  }
  ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x));
  if (ImGui::InputTextWithHint("##newTag", "+ tag", &_newTag, ImGuiInputTextFlags_EnterReturnsTrue) && !_newTag.empty()) {
    editTile(doc, "Add Tag", [&](Json& t) { t["tags"].push_back(_newTag); });
    _newTag.clear();
  }
  ui::propertyRow("Draw under", "Characters for the tile drawn beneath: the first found next to it, else the last (people standing on a road or grass)");
  std::string under = tile.value("under", std::string());
  if (ImGui::InputTextWithHint("##under", "nothing", &under)) {
    editTile(doc, "Set Under", [&](Json& t) {
      if (under.empty()) t.erase("under");
      else t["under"] = under;
    }, "under");
  }
  if (usesMask(tile)) {
    ui::propertyRow("Joins", "Characters counted as the same terrain for edges (default: itself)");
    std::string joins = tile.value("joins", std::string());
    if (ImGui::InputTextWithHint("##joins", _selected.c_str(), &joins)) {
      editTile(doc, "Set Joins", [&](Json& t) {
        if (joins.empty()) t.erase("joins");
        else t["joins"] = joins;
      }, "joins");
    }
  }
  ui::endProperties();

  // Edge rules: the first whose open sides all border other terrain (and closed ones don't) wins.
  ImGui::Dummy({0, 6});
  ui::sectionLabel("Edge rules");
  const Json edges = tile.value("edges", Json::array());
  if (edges.empty()) ui::smallText("Optional: a different image where chosen sides border other terrain (grass tops on ground).", theme::textFaint);
  for (size_t i = 0; i < edges.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    const Json rule = edges[i];
    for (int part = 0; part < 2; ++part) {
      const char* key = part == 0 ? "open" : "closed";
      const int mask = sidesMask(rule.value(key, std::string()));
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(theme::textDim, part == 0 ? "Open " : "Closed");
      ImGui::SameLine(64);
      for (int s = 0; s < 4; ++s) {
        ImGui::PushID(part * 4 + s);
        const bool on = mask & (1 << s);
        if (ui::iconButton("side", kSides[s], part == 0 ? "This side borders other terrain" : "This side doesn't", on)) {
          const int next = mask ^ (1 << s);
          editTile(doc, "Edit Edge Rule", [&](Json& t) {
            if (next) t["edges"][i][key] = maskSides(next);
            else t["edges"][i].erase(key);
          });
        }
        ImGui::PopID();
        ImGui::SameLine(0, 2);
      }
      ImGui::NewLine();
    }
    std::string ruleImage = rule.value("image", std::string());
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::BeginGroup();
    ImGui::PushItemWidth(w - ImGui::GetFrameHeight() - 4);
    if (widgets::regionField("ruleImage", project, atlas, ruleImage)) {
      editTile(doc, "Edit Edge Rule", [&](Json& t) { t["edges"][i]["image"] = ruleImage; }, "edge" + std::to_string(i));
    }
    ImGui::PopItemWidth();
    ImGui::EndGroup();
    ImGui::SameLine(0, 4);
    if (ui::iconButton("remove", ICON_TRASH, "Remove rule")) {
      editTile(doc, "Remove Edge Rule", [&](Json& t) {
        t["edges"].erase(i);
        if (t["edges"].empty()) t.erase("edges");
      });
    }
    ImGui::Dummy({0, 4});
    ImGui::PopID();
  }
  if (ui::button(ICON_PLUS "  Edge Rule")) {
    editTile(doc, "Add Edge Rule", [&](Json& t) { t["edges"].push_back({{"open", "N"}, {"image", t.value("image", Json("")).is_string() ? t.value("image", std::string()) : ""}}); });
  }
}

void TilesetEditor::draw(Editor& editor, AssetDocument& doc) {
  const Project& project = *editor.project();
  // Which maps use this tileset: rescanned when scenes, prefabs or maps change.
  size_t signature = 7;
  for (const AssetFile& f : project.files()) {
    if (f.kind == AssetKind::Scene || f.kind == AssetKind::Prefab || f.kind == AssetKind::Map) {
      signature = signature * 31 + static_cast<size_t>(f.modified.time_since_epoch().count());
    }
  }
  if (signature != _usesSignature) {
    _usesSignature = signature;
    _uses = findUses(project, doc.path());
  }

  const Json& value = doc.value();
  const Json tiles = value.value("tiles", Json::object());
  const std::string atlas = value.value("atlas", std::string());
  if (_selected.empty() && !tiles.empty()) _selected = tiles.begin().key();

  const float right = ui::beginDocumentBar(ICON_GRID_FOUR, "Tileset", doc.path().c_str());
  // The atlas the images come from, and which map's look ({name} vars) to preview.
  const float atlasW = 230.0f, lookW = 150.0f, searchW = 170.0f;
  std::vector<std::string> looks;
  for (const MapUse& u : _uses) {
    const std::string label = lookLabel(u.vars);
    if (!label.empty() && std::find(looks.begin(), looks.end(), label) == looks.end()) looks.push_back(label);
  }
  const float actions = searchW + atlasW + 16 + (looks.size() > 1 ? lookW + 8 : 0);
  ImGui::SameLine(right - actions);
  ui::searchField("filter", _filter, "Find a tile", searchW);
  if (looks.size() > 1) {
    ImGui::SameLine(0, 8);
    ImGui::SetNextItemWidth(lookW);
    if (ui::beginCombo("##look", looks[static_cast<size_t>(_look) % looks.size()].c_str())) {
      for (size_t i = 0; i < looks.size(); ++i) {
        if (ImGui::Selectable(looks[i].c_str(), static_cast<size_t>(_look) == i)) {
          // Point at the first use with that look.
          for (size_t u = 0; u < _uses.size(); ++u) {
            if (lookLabel(_uses[u].vars) == looks[i]) _look = static_cast<int>(u);
          }
        }
      }
      ImGui::EndCombo();
    }
    ui::tooltip("Preview images with this map's vars");
  }
  ImGui::SameLine(0, 8);
  ImGui::SetNextItemWidth(atlasW);
  if (ui::beginCombo("##atlas", atlas.empty() ? "Choose an atlas" : (std::string(ICON_SQUARES_FOUR "  ") + std::filesystem::path(atlas).filename().string()).c_str())) {
    for (const AssetFile& f : project.files()) {
      if (f.kind == AssetKind::Atlas && ImGui::Selectable(f.path.c_str(), f.path == atlas)) {
        doc.edit("Set Atlas", [&](Json& v) { v["atlas"] = f.path; });
      }
    }
    ImGui::EndCombo();
  }
  ui::tooltip("The atlas tile images come from");
  ui::endDocumentBar();

  // The tiles; the selected one's properties are in the Inspector.
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16, 12});
  ImGui::BeginChild("##tiles", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
  drawGrid(project, doc, tiles, atlas);
  ImGui::Dummy({0, 10});
  ui::smallText(ICON_PROHIBIT " solid    " ICON_GRID_NINE " edge-aware    " ICON_FILM_STRIP " animated    Right-click a tile for more.",
                theme::textFaint);
  ImGui::EndChild();
  ImGui::PopStyleVar();
}

bool TilesetEditor::drawInspector(Editor& editor, AssetDocument& doc) {
  const Json tiles = doc.value().value("tiles", Json::object());
  drawDetail(editor, doc, tiles, doc.value().value("atlas", std::string()));
  return true;
}

}  // namespace

std::unique_ptr<AssetEditor> makeTilesetEditor() { return std::make_unique<TilesetEditor>(); }
