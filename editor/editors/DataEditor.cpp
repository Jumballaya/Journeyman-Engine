// The data editor (any other .json): an outline of the file's top-level
// entries; lists of records (enemies, items...) as a spreadsheet, everything
// else as a typed tree. The selected record edits in full in the Inspector.
//
// Edits apply to the live document at once, so a loop over part of it stops
// (or goes by index) once an edit may have moved what it walks.

#include <cmath>
#include <filesystem>
#include <map>
#include <set>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include "AssetEditor.hpp"
#include "Editor.hpp"
#include "EditorWidgets.hpp"
#include "Icons.hpp"
#include "Theme.hpp"
#include "Ui.hpp"

namespace {

using Pointer = Json::json_pointer;

const Project* sProject = nullptr;  // the open project and editor, for the duration of a draw
Editor* sEditor = nullptr;

bool isTable(const Json& v) {
  return v.is_array() && !v.empty() && std::all_of(v.begin(), v.end(), [](const Json& e) { return e.is_object(); });
}

bool allPrimitive(const Json& v) {
  return v.is_array() && std::all_of(v.begin(), v.end(), [](const Json& e) { return e.is_primitive(); });
}

const char* typeIcon(const Json& v) {
  if (isTable(v)) return ICON_TABLE;
  if (v.is_array()) return ICON_LIST_BULLETS;
  if (v.is_object()) return ICON_BRACKETS_CURLY;
  if (v.is_string()) return ICON_TEXT_T;
  if (v.is_boolean()) return ICON_TOGGLE_LEFT;
  if (v.is_number()) return ICON_HASH;
  return ICON_CIRCLE_DASHED;
}

// A short reading of a value for a cell or a collapsed node.
std::string summary(const Json& v) {
  if (v.is_string()) return v.get<std::string>();
  if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
  if (v.is_number_integer()) return std::to_string(v.get<long long>());
  if (v.is_number()) {
    char out[32];
    std::snprintf(out, sizeof(out), "%g", v.get<double>());
    return out;
  }
  if (v.is_array()) {
    std::string out;
    for (const Json& e : v) {
      if (!e.is_primitive()) return std::to_string(v.size()) + " items";
      out += (out.empty() ? "" : ", ") + summary(e);
    }
    return out.empty() ? "empty" : out;
  }
  if (v.is_object()) return std::to_string(v.size()) + (v.size() == 1 ? " field" : " fields");
  return "null";
}

// A value of `like`'s type, emptied: what a new row's cell or a new item starts as.
Json blankLike(const Json& like) {
  if (like.is_string()) return "";
  if (like.is_number_integer()) return 0;
  if (like.is_number()) return 0.0;
  if (like.is_boolean()) return false;
  if (like.is_array()) return Json::array();
  if (like.is_object()) {
    Json out = Json::object();
    for (const auto& [k, v] : like.items()) out[k] = blankLike(v);
    return out;
  }
  return nullptr;
}

// What a value can be made into; a column holds any of the first kColumnTypes.
struct ValueType {
  const char* icon;
  const char* name;
  Json blank;
};
const ValueType kTypes[] = {{ICON_TEXT_T, "Text", ""}, {ICON_HASH, "Number", 0}, {ICON_TOGGLE_LEFT, "True / False", false},
                            {ICON_LIST_BULLETS, "List", Json::array()}, {ICON_BRACKETS_CURLY, "Group", Json::object()}};
constexpr int kColumnTypes = 4;

// `v` as kTypes[type], keeping what it says: numbers to text and back, anything to a list of itself.
Json converted(const Json& v, int type) {
  switch (type) {
    case 0: return v.is_string() ? v : Json(v.is_primitive() && !v.is_null() ? summary(v) : "");
    case 1: {
      if (v.is_number()) return v;
      if (v.is_boolean()) return v.get<bool>() ? 1 : 0;
      if (!v.is_string()) return 0;
      const std::string& t = v.get_ref<const std::string&>();
      char* end = nullptr;
      const double n = std::strtod(t.c_str(), &end);
      if (end == t.c_str() || !std::isfinite(n)) return 0;
      return n == std::floor(n) && std::abs(n) < 1e18 ? Json(static_cast<long long>(n)) : Json(n);
    }
    case 2: return v.is_boolean() ? v : Json(v.is_number() ? v.get<double>() != 0 : (v == "true" || v == "yes"));
    default: return v.is_array() ? v : (v == "" ? Json::array() : Json::array({v}));
  }
}

std::string pointerLabel(const Pointer& p) { return p.empty() ? "root" : p.to_string().substr(1); }

void setValue(AssetDocument& doc, const Pointer& at, const Json& value, const char* verb = "Set", const std::string& mergeKey = {}) {
  doc.edit(verb + (" " + pointerLabel(at)), [&](Json& d) { d[at] = value; }, mergeKey);
}

std::string lowered(std::string s) {
  for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return s;
}

// Whether field name `name` has one of `words` in it ("enemyIcon" names a picture).
bool nameHas(const std::string& name, std::initializer_list<const char*> words) {
  const std::string lower = lowered(name);
  return std::any_of(words.begin(), words.end(), [&](const char* w) { return lower.find(w) != std::string::npos; });
}

bool soundNamed(const std::string& name) { return nameHas(name, {"sound", "sfx", "music", "audio", "voice", "clip"}); }

// Edits a scalar in place, frameless when `inCell`.
void scalarWidget(AssetDocument& doc, const Pointer& at, const Json& v, bool inCell, float width = -1) {
  const std::string key = at.to_string();
  ImGui::PushID(key.c_str());
  if (inCell) {
    ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::withAlpha(theme::bg2, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {4, 3});
  }
  ImGui::SetNextItemWidth(width);
  const auto set = [&](const Json& value, const std::string& mergeKey) { setValue(doc, at, value, "Edit", mergeKey); };
  if (v.is_boolean()) {
    bool b = v.get<bool>();
    if (ui::toggle("##b", &b)) set(b, {});
  } else if (v.is_number_integer()) {
    long long n = v.get<long long>();
    if (ImGui::DragScalar("##n", ImGuiDataType_S64, &n, 0.2f)) set(n, key);
  } else if (v.is_number()) {
    double n = v.get<double>();
    if (ImGui::DragScalar("##f", ImGuiDataType_Double, &n, 0.01f, nullptr, nullptr, "%g")) set(n, key);
  } else if (v.is_string()) {
    std::string s = v.get<std::string>();
    if (!inCell && (s.size() > 32 || s.find('\n') != std::string::npos)) {
      // Prose (descriptions, dialogue) wraps in a box as tall as it needs.
      const float w = width < 0 ? ImGui::GetContentRegionAvail().x : width;
      const float line = ImGui::GetTextLineHeight();
      const ImVec2 pad = ImGui::GetStyle().FramePadding;
      const float h = std::clamp(ImGui::CalcTextSize(s.c_str(), nullptr, false, w - pad.x * 2).y, line * 2, line * 10) + pad.y * 2 + 2;
      if (ImGui::InputTextMultiline("##s", &s, {w, h}, ImGuiInputTextFlags_WordWrap)) set(s, key);
    } else if (ImGui::InputText("##s", &s, inCell ? ImGuiInputTextFlags_AutoSelectAll : 0)) {
      // In a cell, typing replaces the value, as in a spreadsheet; a second click places the cursor.
      set(s, key);
    }
  } else {
    ImGui::TextColored(theme::textFaint, "null");
  }
  if (inCell) {
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
  }
  ImGui::PopID();
}

// Sprites: a text value naming an atlas region shows the picture. The atlas defining `region`, else "".
std::string atlasOf(const std::string& region) {
  if (!sProject || region.empty()) return {};
  for (const AssetFile& f : sProject->files()) {
    if (f.kind != AssetKind::Atlas) continue;
    const auto regions = Thumbnails::instance().regions(*sProject, f.path);
    if (std::binary_search(regions.begin(), regions.end(), region)) return f.path;
  }
  return {};
}

// For a field that's still empty: named like a picture, it picks from the project's first atlas.
std::string atlasForName(const std::string& name) {
  if (!sProject || !nameHas(name, {"icon", "sprite", "portrait", "image", "frame", "picture"})) return {};
  for (const AssetFile& f : sProject->files()) {
    if (f.kind == AssetKind::Atlas) return f.path;
  }
  return {};
}

// Draws `atlas`'s `region` fitted in the `size` square at `a`.
void drawSprite(const std::string& atlas, const std::string& region, ImVec2 a, float size) {
  if (atlas.empty()) return;
  if (auto p = Thumbnails::instance().get(*sProject, atlas + "#" + region)) {
    widgets::fitted(ImGui::GetWindowDrawList(), *p, {a.x + 1, a.y + 1}, {a.x + size - 1, a.y + size - 1});
  }
}

// Sounds: a text value naming one of the project's sounds (by file name, as scripts do) plays it.
std::string soundOf(const std::string& name) {
  if (!sProject || name.empty()) return {};
  for (const AssetFile& f : sProject->files()) {
    if (f.kind == AssetKind::Sound && std::filesystem::path(f.path).stem().string() == name) return f.path;
  }
  return {};
}

// A text field with a picker: `lead(frameHeight)` draws what comes before the field (and its
// SameLine), then a button opens popup "pick", which `popup()` draws; both in `at`'s ID scope.
template <class Lead, class Popup>
void pickerField(AssetDocument& doc, const Pointer& at, const Json& v, bool inCell, const char* icon, const char* tip, const char* fieldTip,
                 Lead&& lead, Popup&& popup) {
  const float fh = ImGui::GetFrameHeight();
  const std::string id = at.to_string();
  ImGui::BeginGroup();
  ImGui::PushID(id.c_str());
  ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, true);
  lead(fh);
  ImGui::PopItemFlag();
  ImGui::PopID();
  // The field keeps the plain one's ID: a column turning into sprites mid-typing doesn't drop the cursor.
  scalarWidget(doc, at, v, inCell, ImGui::GetContentRegionAvail().x - fh);
  if (fieldTip && ImGui::IsItemHovered()) ui::tooltip(fieldTip);
  ImGui::SameLine(0, 0);
  ImGui::PushID(id.c_str());
  ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, true);
  if (ui::iconButton("pick", icon, tip, false, 0, fh)) ImGui::OpenPopup("pick");
  ImGui::PopItemFlag();
  popup();
  ImGui::PopID();
  ImGui::EndGroup();
}

// Begins popup "pick" with a search field, focused and cleared each time it opens.
bool beginSearchPopup(const std::string& hint, std::string& filter, ImVec2 minSize, ImVec2 maxSize) {
  ImGui::SetNextWindowSizeConstraints(minSize, maxSize);
  if (!ImGui::BeginPopup("pick")) return false;
  if (ImGui::IsWindowAppearing()) filter.clear(), ImGui::SetKeyboardFocusHere();
  ui::searchField("##filter", filter, hint.c_str());
  return true;
}

void spriteWidget(AssetDocument& doc, const Pointer& at, const Json& v, const std::string& atlas, bool inCell) {
  const std::string value = v.get<std::string>();
  pickerField(
      doc, at, v, inCell, ICON_SQUARES_FOUR, "Pick a sprite", nullptr,
      [&](float fh) {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::Dummy({fh, fh});
        drawSprite(atlas, value, a, fh);
        ImGui::SameLine(0, 2);
      },
      [&] {
        std::string picked = value;
        if (widgets::regionPopup("pick", *sProject, atlas, picked)) setValue(doc, at, picked);
      });
}

void soundWidget(AssetDocument& doc, const Pointer& at, const Json& v, bool inCell) {
  const std::string value = v.get<std::string>();
  pickerField(
      doc, at, v, inCell, ICON_CARET_DOWN, "Pick a sound", nullptr,
      [&](float fh) {
        const std::string file = soundOf(value);
        ImGui::BeginDisabled(file.empty());
        if (ui::iconButton("play", ICON_PLAY, "Play", false, 0, fh)) sEditor->previewSound(file);
        ImGui::EndDisabled();
        ImGui::SameLine(0, 2);
      },
      [&] {
        static std::string filter;
        if (!beginSearchPopup("Search sounds", filter, {240, 0}, {360, 420})) return;
        for (const AssetFile& f : sProject->files()) {
          if (f.kind != AssetKind::Sound) continue;
          const std::string name = std::filesystem::path(f.path).stem().string();
          if (!filter.empty() && ui::fuzzyScore(name, filter) < 0) continue;
          ImGui::PushID(f.path.c_str());
          if (ui::iconButton("play", ICON_PLAY, nullptr, false, 0, ImGui::GetFrameHeight() - 4)) sEditor->previewSound(f.path);
          ImGui::SameLine(0, 4);
          if (ImGui::Selectable(name.c_str(), name == value)) {
            setValue(doc, at, name);
            ImGui::CloseCurrentPopup();
          }
          ImGui::PopID();
        }
        ImGui::EndPopup();
      });
}

// Records: tables in the project's data files whose rows have an "id". A column of
// such ids (an ability's "requires": "revolver") is a reference, picked by name.
struct RecordTable {
  struct Record {
    std::string id, name, icon;
  };
  std::string file, key;  // "assets/data/items.json", "items"
  std::vector<Record> records;
  bool hasIcons = false;
  const Record* find(const std::string& id) const {
    for (const Record& r : records) {
      if (r.id == id) return &r;
    }
    return nullptr;
  }
};

// Every record table in the project, re-read when its file changes; computed once a frame.
const std::vector<RecordTable>& recordTables() {
  static std::map<std::string, std::pair<std::filesystem::file_time_type, std::vector<RecordTable>>> byFile;
  static std::vector<RecordTable> all;
  static int frame = -1;
  if (frame == ImGui::GetFrameCount() || !sProject) return all;
  frame = ImGui::GetFrameCount();
  all.clear();
  for (const AssetFile& f : sProject->files()) {
    if (f.kind != AssetKind::Data) continue;
    auto& [modified, tables] = byFile[f.path];
    if (modified != f.modified) {
      modified = f.modified;
      tables.clear();
      const Json root = Json::parse(sProject->readText(f.path), nullptr, false);
      if (!root.is_object()) continue;
      for (const auto& [key, rows] : root.items()) {
        if (!rows.is_array()) continue;
        RecordTable table{.file = f.path, .key = key, .records = {}};
        for (const Json& row : rows) {
          if (!row.is_object() || !row.contains("id") || !row["id"].is_string()) continue;
          // Its picture: an icon, else a portrait or sprite (people).
          std::string icon;
          for (const char* field : {"icon", "portrait", "sprite"}) {
            if (icon.empty() && row.contains(field) && row[field].is_string()) icon = row[field];
          }
          table.hasIcons |= !icon.empty();
          table.records.push_back({row["id"], row.value("name", row.value("title", std::string())), icon});
        }
        if (!table.records.empty()) tables.push_back(std::move(table));
      }
    }
    all.insert(all.end(), tables.begin(), tables.end());
  }
  return all;
}

void refWidget(AssetDocument& doc, const Pointer& at, const Json& v, const RecordTable& table, bool inCell) {
  const std::string value = v.get<std::string>();
  const RecordTable::Record* current = table.find(value);
  pickerField(
      doc, at, v, inCell, ICON_CARET_DOWN, ("Pick from " + table.key).c_str(), current && !current->name.empty() ? current->name.c_str() : nullptr,
      [&](float fh) {
        // A badge before the id: the record's icon, or a warning when no record has that id.
        if (!table.hasIcons && (current || value.empty())) return;
        const ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::Dummy({fh, fh});
        if (current) drawSprite(atlasOf(current->icon), current->icon, a, fh);
        else if (!value.empty()) ImGui::GetWindowDrawList()->AddText({a.x + 5, a.y + 3}, theme::u32(theme::warning), ICON_WARNING);
        if (ImGui::IsItemHovered()) {
          ui::tooltip(current ? (current->name.empty() ? current->id : current->name).c_str()
                              : (value.empty() ? "None" : ("No " + table.key + " record is called \"" + value + "\"").c_str()));
        }
        ImGui::SameLine(0, 2);
      },
      [&] {
        static std::string filter;
        if (!beginSearchPopup("Search " + table.key, filter, {260, 0}, {380, 440})) return;
        ui::smallText(ui::displayPath(table.file).c_str(), theme::textFaint);
        if (ImGui::Selectable("None", value.empty())) {
          setValue(doc, at, "", "Clear");
          ImGui::CloseCurrentPopup();
        }
        const float fh = ImGui::GetFrameHeight();
        for (const RecordTable::Record& r : table.records) {
          if (!filter.empty() && ui::fuzzyScore(r.id + " " + r.name, filter) < 0) continue;
          ImGui::PushID(r.id.c_str());
          const ImVec2 p = ImGui::GetCursorScreenPos();
          if (ImGui::Selectable("##record", r.id == value, 0, {0, fh})) {
            setValue(doc, at, r.id);
            ImGui::CloseCurrentPopup();
          }
          drawSprite(atlasOf(r.icon), r.icon, p, fh);
          ImDrawList* draw = ImGui::GetWindowDrawList();
          const float ty = p.y + (fh - ImGui::GetTextLineHeight()) * 0.5f;
          draw->AddText({p.x + fh + 6, ty}, theme::u32(theme::text), (r.name.empty() ? r.id : r.name).c_str());
          if (!r.name.empty()) draw->AddText({p.x + fh + 14 + ImGui::CalcTextSize(r.name.c_str()).x, ty}, theme::u32(theme::textFaint), r.id.c_str());
          ImGui::PopID();
        }
        ImGui::EndPopup();
      });
}

// A table's columns (every key any record has, in first-seen order) and what its text
// columns hold, decided from all their values together.
struct Columns {
  std::vector<std::string> names;
  std::map<std::string, std::string> sprite;  // column -> atlas
  std::set<std::string> sound;
  std::map<std::string, const RecordTable*> ref;            // column -> the table its ids come from (this frame's)
  std::map<std::string, std::vector<std::string>> choices;  // text repeating a few values ("fire", "ice")
};

Columns columnsOf(const Json& rows) {
  Columns cols;
  for (const Json& row : rows) {
    for (const auto& [k, _] : row.items()) {
      if (std::find(cols.names.begin(), cols.names.end(), k) == cols.names.end()) cols.names.push_back(k);
    }
  }
  for (const std::string& c : cols.names) {
    std::vector<std::string> values, distinct;
    size_t filled = 0;
    bool text = true;
    for (const Json& row : rows) {
      if (!row.contains(c)) continue;
      ++filled;
      if (!row[c].is_string()) {
        text = false;
        break;
      }
      const std::string& s = row[c].get_ref<const std::string&>();
      if (!s.empty()) values.push_back(s);
      if (std::find(distinct.begin(), distinct.end(), s) == distinct.end()) distinct.push_back(s);
    }
    if (!text) continue;
    if (distinct.size() >= 2 && distinct.size() <= 12 && distinct.size() < filled) cols.choices[c] = distinct;
    // Every value has to match (one that happens to name a sound isn't enough); empty columns go by their name.
    auto all = [&](auto&& matches) { return !values.empty() && std::all_of(values.begin(), values.end(), matches); };
    const std::string atlas = values.empty() ? atlasForName(c) : all([](const std::string& v) { return !atlasOf(v).empty(); }) ? atlasOf(values.front()) : "";
    if (!atlas.empty()) {
      cols.sprite[c] = atlas;
    } else if (all([](const std::string& v) { return !soundOf(v).empty(); }) || (values.empty() && soundNamed(c))) {
      cols.sound.insert(c);
    } else if (const std::string lower = lowered(c); lower != "id") {
      for (const RecordTable& table : recordTables()) {
        const bool named = table.key == lower || table.key == lower + "s" || table.key == lower + "es";
        if (all([&](const std::string& v) { return table.find(v) != nullptr; }) || (values.empty() && named)) {
          cols.ref[c] = &table;
          break;
        }
      }
    }
  }
  return cols;
}

// Draws the column's own widget for a text value; false when the column is plain text.
bool kindWidget(AssetDocument& doc, const Pointer& at, const Json& v, const std::string& column, const Columns* cols, bool inCell) {
  if (!cols || !v.is_string()) return false;
  if (auto it = cols->sprite.find(column); it != cols->sprite.end()) spriteWidget(doc, at, v, it->second, inCell);
  else if (cols->sound.contains(column)) soundWidget(doc, at, v, inCell);
  else if (auto r = cols->ref.find(column); r != cols->ref.end()) refWidget(doc, at, v, *r->second, inCell);
  else return false;
  return true;
}

// The menu every tree row has: change type, duplicate (in lists), delete. True when it changed
// the value's place or type, which moves or retypes what the caller is walking.
bool valueMenu(AssetDocument& doc, const Pointer& at, const Json& v, bool inArray) {
  if (!ImGui::BeginPopupContextItem("value")) return false;
  bool changed = false;
  if (ImGui::BeginMenu(ICON_SHAPES "  Change Type")) {
    for (const ValueType& type : kTypes) {
      if (at.empty() && type.blank.is_string()) continue;  // a text root would save as the file's raw text
      if (ImGui::MenuItem(type.name)) setValue(doc, at, type.blank, "Change Type of"), changed = true;
    }
    ImGui::EndMenu();
  }
  if (inArray && ImGui::MenuItem(ICON_COPY "  Duplicate")) {
    doc.edit("Duplicate " + pointerLabel(at), [&](Json& d) {
      Json& list = d[at.parent_pointer()];
      list.insert(list.begin() + std::stol(at.back()) + 1, v);
    });
    changed = true;
  }
  if (!at.empty() && ImGui::MenuItem(ICON_TRASH "  Delete")) {
    doc.edit("Delete " + pointerLabel(at), [&](Json& d) {
      Json& parent = d[at.parent_pointer()];
      if (parent.is_array()) parent.erase(std::stoul(at.back()));
      else parent.erase(at.back());
    });
    changed = true;
  }
  ImGui::EndPopup();
  return changed;
}

// A list of plain values as a row of chips: click one to edit it, x removes, + adds.
void chipList(AssetDocument& doc, const Pointer& at, const Json& list) {
  for (size_t i = 0; i < list.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    bool removed = false;
    if (ui::chip("item", summary(list[i]).c_str(), true, &removed)) ImGui::OpenPopup("editItem");
    if (removed) {
      doc.edit("Remove from " + pointerLabel(at), [&](Json& d) { d[at].erase(i); });
      ImGui::PopID();
      break;  // the items after it moved
    }
    if (ImGui::BeginPopup("editItem")) {
      if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
      ImGui::SetNextItemWidth(200);
      scalarWidget(doc, at / i, list[i], false);
      if (ImGui::IsKeyPressed(ImGuiKey_Enter)) ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
    ImGui::PopID();
    ImGui::SameLine(0, 4);
  }
  if (ui::iconButton("add", ICON_PLUS, "Add an item", false, 0, ImGui::GetFrameHeight() - 2)) {
    doc.edit("Add to " + pointerLabel(at), [&](Json& d) { d[at].push_back(list.empty() ? Json("") : list.back()); });
  }
}

// "+ Field": names a new text field of `object` (at `at`).
void fieldAdder(AssetDocument& doc, const Pointer& at, const Json& object) {
  static std::string name;
  if (ui::button(ICON_PLUS "  Field")) ImGui::OpenPopup("newField"), name.clear();
  if (!ImGui::BeginPopup("newField")) return;
  if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
  ImGui::SetNextItemWidth(180);
  if (ImGui::InputTextWithHint("##name", "field name", &name, ImGuiInputTextFlags_EnterReturnsTrue) && !name.empty() && !object.contains(name)) {
    doc.edit("Add Field " + name, [&](Json& d) { d[at][name] = ""; });
    ImGui::CloseCurrentPopup();
  }
  ui::smallText("Enter adds it as text; right-click it to change its type.", theme::textFaint);
  ImGui::EndPopup();
}

// A plain value field `label`: edited as its record column is, else as what it names (a sound,
// by its field's name too).
void leafWidget(AssetDocument& doc, const Pointer& at, const Json& v, const std::string& label, const Columns* cols) {
  if (kindWidget(doc, at, v, label, cols, false)) return;
  const std::string text = v.is_string() ? v.get<std::string>() : "";
  const std::string atlas = !v.is_string() ? "" : text.empty() ? atlasForName(label) : atlasOf(text);
  if (!atlas.empty()) spriteWidget(doc, at, v, atlas, false);
  else if (v.is_string() && soundNamed(label) && (text.empty() || !soundOf(text).empty())) soundWidget(doc, at, v, false);
  else scalarWidget(doc, at, v, false);
}

// A typed tree: groups and lists fold; values edit in place. A field named like one of `cols`
// edits as that column does. True when an edit moved or retyped values: the caller stops walking.
bool treeEditor(AssetDocument& doc, const Pointer& at, const Json& v, const std::string& label, bool inArray, const Columns* cols) {
  ImGui::PushID(at.to_string().c_str());
  bool changed = false;
  if (v.is_object() || (v.is_array() && !allPrimitive(v))) {
    const bool open = ImGui::TreeNodeEx("##node", ImGuiTreeNodeFlags_SpanAvailWidth | (at.empty() || v.size() < 12 ? ImGuiTreeNodeFlags_DefaultOpen : 0),
                                        "%s  %s", typeIcon(v), label.c_str());
    changed = valueMenu(doc, at, v, inArray);
    if (!changed) {
      ImGui::SameLine();
      ImGui::TextColored(theme::textFaint, "%s", summary(v).c_str());
    }
    if (open && !changed && v.is_object()) {
      for (const auto& [k, child] : v.items()) {
        if ((changed = treeEditor(doc, at / k, child, k, false, cols))) break;
      }
      if (!changed) fieldAdder(doc, at, v);
    } else if (open && !changed) {
      for (size_t i = 0; i < v.size() && !changed; ++i) changed = treeEditor(doc, at / i, v[i], std::to_string(i), true, cols);
      if (!changed && ui::button(ICON_PLUS "  Item")) {
        doc.edit("Add Item to " + pointerLabel(at), [&](Json& d) { d[at].push_back(blankLike(v.back())); });
      }
    }
    if (open) ImGui::TreePop();
  } else {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::textFaint, "%s", typeIcon(v));
    ImGui::SameLine(0, 6);
    ImGui::TextColored(inArray ? theme::textFaint : theme::textDim, "%s", label.c_str());
    changed = valueMenu(doc, at, v, inArray);
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8, 170.0f));
    if (!changed && v.is_array()) chipList(doc, at, v);
    else if (!changed) leafWidget(doc, at, v, label, cols);
  }
  ImGui::PopID();
  return changed;
}

// A column header's menu: rename, change type, move, delete. Columns are the records' key
// order, so each rewrites every record.
void columnMenu(AssetDocument& doc, const Pointer& at, const std::vector<std::string>& columns, size_t ci) {
  if (!ImGui::BeginPopupContextItem("column")) return;
  const std::string& c = columns[ci];
  static std::string rename;
  if (ImGui::IsWindowAppearing()) rename = c, ImGui::SetKeyboardFocusHere();
  ImGui::SetNextItemWidth(160);
  if (ImGui::InputText("##rename", &rename, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll) && !rename.empty() &&
      std::find(columns.begin(), columns.end(), rename) == columns.end()) {
    doc.edit("Rename Column " + c, [&](Json& d) {
      for (Json& row : d[at]) {
        Json renamed = Json::object();
        for (auto& [k, val] : row.items()) renamed[k == c ? rename : k] = std::move(val);
        row = std::move(renamed);
      }
    });
    ImGui::CloseCurrentPopup();
  }
  if (ImGui::BeginMenu(ICON_SHAPES "  Change Type")) {
    // Every record's value converts, keeping what it says.
    for (int k = 0; k < kColumnTypes; ++k) {
      if (!ImGui::MenuItem(kTypes[k].name)) continue;
      doc.edit("Change Type of " + c, [&](Json& d) {
        for (Json& row : d[at]) {
          if (row.contains(c)) row[c] = converted(row[c], k);
        }
      });
    }
    ImGui::EndMenu();
  }
  for (const int dir : {-1, 1}) {
    const size_t other = ci + dir;  // wraps past the start: out of range too
    if (!ImGui::MenuItem(dir < 0 ? ICON_ARROW_LEFT "  Move Left" : ICON_ARROW_RIGHT "  Move Right", nullptr, false, other < columns.size())) continue;
    std::vector<std::string> order = columns;
    std::swap(order[ci], order[other]);
    doc.edit("Move Column " + c, [&](Json& d) {
      for (Json& row : d[at]) {
        Json moved = Json::object();
        for (const std::string& k : order) {
          if (row.contains(k)) moved[k] = std::move(row[k]);
        }
        row = std::move(moved);
      }
    });
  }
  ImGui::Separator();
  if (ImGui::MenuItem(ICON_TRASH "  Delete Column")) {
    doc.edit("Delete Column " + c, [&](Json& d) {
      for (Json& row : d[at]) row.erase(c);
    });
  }
  ImGui::EndPopup();
}

// The record's name-ish field, else its first text, else its number.
std::string recordTitle(const Json& row, int index) {
  const auto isText = [](const Json& v) { return v.is_string() && !v.get_ref<const std::string&>().empty(); };
  for (const char* key : {"name", "title", "label", "id"}) {
    if (row.contains(key) && isText(row[key])) return row[key];
  }
  const auto first = std::find_if(row.begin(), row.end(), isText);
  return first != row.end() ? first->get<std::string>() : "Record " + std::to_string(index + 1);
}

class DataEditor final : public AssetEditor {
 public:
  void draw(Editor& editor, AssetDocument& doc) override;
  bool drawInspector(Editor& editor, AssetDocument& doc) override;
  bool handles(const std::string& command) const override { return _row >= 0 && (command == "edit.duplicate" || command == "edit.delete"); }
  void run(const std::string& command, AssetDocument& doc) override {
    const Pointer at = shownAt(doc.value());
    const Json& rows = doc.value()[at];
    if (!isTable(rows) || _row >= static_cast<int>(rows.size())) return;
    const size_t r = static_cast<size_t>(_row);
    if (command == "edit.delete") {
      doc.edit("Delete Row", [&](Json& d) { d[at].erase(r); });
      _row = std::min(_row, static_cast<int>(rows.size()) - 1);  // `rows` is live: already one shorter
    } else {
      doc.edit("Duplicate Row", [&](Json& d) { d[at].insert(d[at].begin() + static_cast<long>(r) + 1, d[at][r]); });
      ++_row;
    }
  }

 private:
  std::string _section;     // top-level key shown (or "" for the root)
  int _row = -1;            // selected record in a table
  int _focusRow = -1;       // a row whose first cell takes the keyboard next frame
  std::string _filter;
  std::string _newColumn;
  std::string _newTable;
  int _newColumnKind = 0;     // into kTypes
  bool _refocusName = false;  // a type was clicked: back to the name, so Enter adds
  std::vector<std::string> _shownColumns;  // last frame's, to notice a reorder

  // Where the shown value is: the selected top-level entry, else the whole file.
  Pointer shownAt(const Json& root) const { return root.is_object() && root.contains(_section) ? Pointer() / _section : Pointer(); }
  // A blank record after the last, selected, with the keyboard in its first cell.
  void addRow(AssetDocument& doc, const Pointer& at, const Json& rows) {
    _row = _focusRow = static_cast<int>(rows.size());  // before the edit: `rows` is the live document
    doc.edit("Add Row", [&](Json& d) { d[at].push_back(blankLike(rows.back())); });
  }
  void drawTable(AssetDocument& doc, const Pointer& at, const Json& rows);
  void drawCell(AssetDocument& doc, const Pointer& at, const Json& rows, size_t r, const std::string& c, const Columns& cols);
  void newColumnPopup(AssetDocument& doc, const Pointer& at, const std::vector<std::string>& columns);
};

void DataEditor::drawCell(AssetDocument& doc, const Pointer& at, const Json& rows, size_t r, const std::string& c, const Columns& cols) {
  const Json& row = rows[r];
  const Pointer cell = at / r / c;
  if (!row.contains(c) || !row[c].is_primitive()) {
    // A missing cell: click to give this record the column too. Lists and groups read here; they edit in the Inspector.
    const bool missing = !row.contains(c);
    const std::string text = missing ? "\xE2\x80\x94" : summary(row[c]);
    ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, true);
    ImGui::PushStyleColor(ImGuiCol_Text, missing ? theme::textFaint : theme::textDim);
    if (ImGui::Selectable(ui::ellipsize(text, ImGui::GetContentRegionAvail().x).c_str(), false, ImGuiSelectableFlags_AllowOverlap)) {
      if (!missing) {
        _row = static_cast<int>(r);
      } else {
        const auto other = std::find_if(rows.begin(), rows.end(), [&](const Json& o) { return o.contains(c); });
        const Json like = other == rows.end() ? Json(0) : blankLike((*other)[c]);
        doc.edit("Add " + c, [&](Json& d) { d[cell] = like; });
      }
    }
    ImGui::PopStyleColor();
    ImGui::PopItemFlag();
    ui::tooltip(missing ? "Not set for this record. Click to add it." : (text + "\nEdit it in the Inspector").c_str());
    return;
  }
  if (kindWidget(doc, cell, row[c], c, &cols, true)) return;
  const auto choices = cols.choices.find(c);
  if (!row[c].is_string() || choices == cols.choices.end()) {
    scalarWidget(doc, cell, row[c], true);
    return;
  }
  // Free text, with the column's values one click away.
  pickerField(doc, cell, row[c], true, ICON_CARET_DOWN, nullptr, nullptr, [](float) {}, [&] {
    if (!ImGui::BeginPopup("pick")) return;
    for (const std::string& v : choices->second) {
      if (ImGui::Selectable(v.c_str(), row[c] == v)) doc.edit("Set " + c, [&](Json& d) { d[cell] = v; });
    }
    ImGui::EndPopup();
  });
}

void DataEditor::drawTable(AssetDocument& doc, const Pointer& at, const Json& rows) {
  const Columns cols = columnsOf(rows);
  const std::vector<std::string>& columns = cols.names;
  const float fh = ImGui::GetFrameHeight();
  bool addColumn = false;
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {6, 3});
  const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInner | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
                                ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
  if (ImGui::BeginTable("##table", static_cast<int>(columns.size()) + 2, flags, {0, ImGui::GetContentRegionAvail().y - 40})) {
    // ImGui keeps a column where it was shown when its index changes; the records' key order is the truth.
    if (columns != _shownColumns) {
      if (!_shownColumns.empty()) ImGui::GetCurrentTable()->IsResetDisplayOrderRequest = true;
      _shownColumns = columns;
    }
    ImGui::TableSetupScrollFreeze(columns.empty() ? 1 : 2, 1);  // the row number and the first column (its id) stay put
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 34);
    for (const std::string& c : columns) {
      // Numbers narrow; text as wide as its typical value (prose wider, to a point).
      bool wide = false;
      size_t chars = 0, counted = 0;
      for (const Json& row : rows) {
        if (!row.contains(c)) continue;
        wide |= row[c].is_string() || !row[c].is_primitive();
        if (row[c].is_string()) chars += row[c].get_ref<const std::string&>().size(), ++counted;
      }
      const float typical = counted ? ImGui::CalcTextSize("x").x * static_cast<float>(chars) / static_cast<float>(counted) : 0.0f;
      ImGui::TableSetupColumn(c.c_str(), ImGuiTableColumnFlags_WidthFixed, wide ? std::clamp(typical + 40.0f, 150.0f, 340.0f) : 64.0f);
    }
    ImGui::TableSetupColumn("##add", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, fh);
    // Header: names with a menu each, and a + to add a column.
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    ImGui::TableNextColumn();
    for (size_t ci = 0; ci < columns.size(); ++ci) {
      ImGui::TableNextColumn();
      ImGui::PushID(columns[ci].c_str());
      ImGui::PushFont(theme::fonts().medium, theme::sizeSmall);
      ImGui::TableHeader(columns[ci].c_str());
      ImGui::PopFont();
      columnMenu(doc, at, columns, ci);
      ImGui::PopID();
    }
    ImGui::TableNextColumn();
    addColumn |= ui::iconButton("addColumn", ICON_PLUS, "Add a column to every record");

    for (size_t r = 0; r < rows.size(); ++r) {
      if (!_filter.empty() && ui::fuzzyScore(rows[r].dump(), _filter) < 0) continue;
      const int ri = static_cast<int>(r);
      ImGui::TableNextRow(0, fh);
      ImGui::PushID(ri);
      ImGui::TableNextColumn();
      const bool selected = _row == ri;
      if (selected) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, theme::u32(theme::selection));
      ImGui::PushStyleColor(ImGuiCol_Text, selected ? theme::accent : theme::textFaint);
      // Tab walks the cells, not the row handles: a row reads like a form.
      ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, true);
      if (ImGui::Selectable(std::to_string(r + 1).c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, {0, fh})) {
        _row = ri;
      }
      ImGui::PopItemFlag();
      ImGui::PopStyleColor();
      ImGui::OpenPopupOnItemClick("row");  // drawn after the cells, which an edit from it would move
      for (const std::string& c : columns) {
        ImGui::TableNextColumn();
        ImGui::PushID(c.c_str());
        // A new row: typing goes straight into its first cell.
        if (_focusRow == ri && c == columns.front() && rows[r].contains(c) && rows[r][c].is_primitive()) {
          ImGui::SetKeyboardFocusHere();
          _focusRow = -1;
        }
        // The table's last cell: Tab out of it starts a new row, as in a spreadsheet.
        const bool lastCell = r + 1 == rows.size() && c == columns.back() && _filter.empty();
        if (lastCell) ImGui::BeginGroup();
        drawCell(doc, at, rows, r, c, cols);
        if (lastCell) {
          ImGui::EndGroup();  // IsItemActive() now covers anything in the cell
          if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Tab, false) && !ImGui::GetIO().KeyShift) addRow(doc, at, rows);
        }
        ImGui::PopID();
      }
      if (ImGui::BeginPopup("row")) {
        _row = ri;
        if (ImGui::MenuItem(ICON_COPY "  Duplicate")) run("edit.duplicate", doc);
        for (const int dir : {-1, 1}) {
          const size_t other = r + dir;  // wraps past the start: out of range too
          if (!ImGui::MenuItem(dir < 0 ? ICON_ARROW_UP "  Move Up" : ICON_ARROW_DOWN "  Move Down", nullptr, false, other < rows.size())) continue;
          doc.edit("Move Row", [&](Json& d) { std::swap(d[at][r], d[at][other]); });
          _row = static_cast<int>(other);
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_TRASH "  Delete Row")) run("edit.delete", doc);
        ImGui::EndPopup();
      }
      ImGui::TableNextColumn();
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::PopStyleVar();
  ImGui::Dummy({0, 4});
  if (ui::button(ICON_PLUS "  Add Row")) {
    addRow(doc, at, rows);
    _filter.clear();
  }
  ImGui::SameLine();
  // Also here: on a wide table the header's + is scrolled out of sight.
  addColumn |= ui::button(ICON_PLUS "  Add Column");
  if (addColumn) ImGui::OpenPopup("newColumn"), _newColumn.clear(), _newColumnKind = 0;
  newColumnPopup(doc, at, columns);
  ImGui::SameLine();
  ui::smallText("Right-click a row or column header for more. Lists and groups edit in the Inspector.", theme::textFaint);
}

// Names a new column and picks what it holds; every record gets a blank of that type.
void DataEditor::newColumnPopup(AssetDocument& doc, const Pointer& at, const std::vector<std::string>& columns) {
  if (!ImGui::BeginPopup("newColumn")) return;
  if (ImGui::IsWindowAppearing() || _refocusName) ImGui::SetKeyboardFocusHere(), _refocusName = false;
  ImGui::SetNextItemWidth(180);
  bool add = ImGui::InputTextWithHint("##name", "column name", &_newColumn, ImGuiInputTextFlags_EnterReturnsTrue);
  for (int k = 0; k < kColumnTypes; ++k) {
    ImGui::SameLine(0, k == 0 ? 8 : 2);
    if (ui::iconButton(kTypes[k].name, kTypes[k].icon, kTypes[k].name, _newColumnKind == k)) _newColumnKind = k, _refocusName = true;
  }
  ImGui::SameLine(0, 8);
  add |= ui::primaryButton("Add");
  if (add && !_newColumn.empty() && std::find(columns.begin(), columns.end(), _newColumn) == columns.end()) {
    doc.edit("Add Column " + _newColumn, [&](Json& d) {
      for (Json& row : d[at]) {
        if (!row.contains(_newColumn)) row[_newColumn] = kTypes[_newColumnKind].blank;
      }
    });
    ImGui::CloseCurrentPopup();
  }
  if (ui::dismissPressed()) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

void DataEditor::draw(Editor& editor, AssetDocument& doc) {
  sProject = editor.project();
  sEditor = &editor;
  const Json& root = doc.value();
  const float right = ui::beginDocumentBar(ICON_BRACKETS_CURLY, doc.title().c_str(), doc.path().c_str());
  if (root.is_object()) {
    // A file holds several tables (quests and their stages, lines and their choices); this adds one.
    ImGui::SameLine(right - 110);
    if (ui::button(ICON_PLUS "  Table", {110, 0})) ImGui::OpenPopup("newTable"), _newTable.clear();
    if (ImGui::BeginPopup("newTable")) {
      if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
      ImGui::SetNextItemWidth(200);
      const bool enter = ImGui::InputTextWithHint("##name", "table name", &_newTable, ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::SameLine(0, 8);
      const bool taken = root.contains(_newTable);
      ImGui::BeginDisabled(_newTable.empty() || taken);
      if ((ui::primaryButton("Add") || enter) && !_newTable.empty() && !taken) {
        doc.edit("Add Table " + _newTable, [&](Json& d) { d[_newTable] = Json::parse(R"([{"id": "first", "name": "First"}])"); });
        _section = _newTable, _row = -1;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndDisabled();
      if (taken) ui::smallText(ICON_WARNING " This file already has one by that name.", theme::warning);
      if (ui::dismissPressed()) ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
  }
  ui::endDocumentBar();

  // An outline of the top-level entries when the file is a group of them.
  const bool outline = root.is_object() && !root.empty();
  if (outline && !root.contains(_section)) _section = root.begin().key();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10, 10});
  if (outline && root.size() > 1) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::bg1);
    ImGui::BeginChild("##outline", {200, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
    ui::sectionLabel("Contents");
    for (const auto& [k, v] : root.items()) {
      const std::string item = std::string(typeIcon(v)) + "  " + k;
      if (ImGui::Selectable(item.c_str(), k == _section, 0, {0, 26})) _section = k, _row = -1;
      if (!ImGui::BeginPopupContextItem(k.c_str())) continue;
      const std::string key = k;  // `k` goes with the entry it names
      bool changed = false;       // the object changed under the loop: stop walking it
      static std::string rename;
      if (ImGui::IsWindowAppearing()) rename = key, ImGui::SetKeyboardFocusHere();
      ImGui::SetNextItemWidth(180);
      if (ImGui::InputText("##rename", &rename, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll) && !rename.empty() &&
          !root.contains(rename)) {
        // Rebuilt in order, so the renamed table keeps its place.
        const std::string to = rename;
        doc.edit("Rename " + key, [&](Json& d) {
          Json renamed = Json::object();
          for (auto& [name, val] : d.items()) renamed[name == key ? to : name] = std::move(val);
          d = std::move(renamed);
        });
        if (_section == key) _section = to;
        ImGui::CloseCurrentPopup();
        changed = true;
      }
      if (!changed && ImGui::MenuItem(ICON_TRASH "  Delete")) {
        doc.edit("Delete " + key, [&](Json& d) { d.erase(key); });
        if (_section == key) _section = root.begin().key(), _row = -1;  // the outline shows two or more: one is left
        changed = true;
      }
      ImGui::EndPopup();
      if (changed) break;
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 0);
  }
  ImGui::BeginChild("##body", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
  const Pointer at = shownAt(root);
  const Json& shown = root[at];
  const std::string title = at.empty() ? doc.title() : _section;
  if (isTable(shown)) {
    ImGui::PushFont(theme::fonts().semibold, theme::sizeTitle);
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextColored(theme::textFaint, "%zu %s", shown.size(), shown.size() == 1 ? "record" : "records");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 200);
    ui::searchField("filter", _filter, "Find records", 200);
    ImGui::Dummy({0, 2});
    const bool anyMatch = _filter.empty() || std::any_of(shown.begin(), shown.end(), [&](const Json& row) { return ui::fuzzyScore(row.dump(), _filter) >= 0; });
    if (anyMatch) {
      drawTable(doc, at, shown);
    } else if (ui::emptyState(ICON_MAGNIFYING_GLASS, "No records match", "Search looks through every field of every record.", "Clear Search")) {
      _filter.clear();
    }
  } else {
    treeEditor(doc, at, shown, title, false, nullptr);
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
}

bool DataEditor::drawInspector(Editor& editor, AssetDocument& doc) {
  sProject = editor.project();
  sEditor = &editor;
  const Pointer table = shownAt(doc.value());
  const Json& shown = doc.value()[table];
  if (!isTable(shown)) return false;
  if (_row < 0 || _row >= static_cast<int>(shown.size())) {
    ui::emptyState(ICON_TABLE, "Pick a record", "Select a row to edit all of its fields here, lists and groups included.");
    return true;
  }
  const Pointer at = table / static_cast<size_t>(_row);
  const Json& row = shown[static_cast<size_t>(_row)];
  ImGui::PushFont(nullptr, 20.0f);
  ImGui::TextColored(theme::accent, ICON_TABLE);
  ImGui::PopFont();
  ImGui::SameLine(0, 10);
  ImGui::BeginGroup();
  ImGui::PushFont(theme::fonts().semibold, theme::sizeTitle);
  ImGui::TextUnformatted(recordTitle(row, _row).c_str());
  ImGui::PopFont();
  ui::smallText((pointerLabel(table) + " #" + std::to_string(_row + 1)).c_str(), theme::textFaint);
  ImGui::EndGroup();
  ImGui::Dummy({0, 6});
  const Columns cols = columnsOf(shown);
  for (const auto& [k, v] : row.items()) {
    if (treeEditor(doc, at / k, v, k, false, &cols)) break;
  }
  fieldAdder(doc, at, row);
  return true;
}

}  // namespace

std::unique_ptr<AssetEditor> makeDataEditor() { return std::make_unique<DataEditor>(); }
