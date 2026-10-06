// The data editor (any other .json): an outline of the file's top-level
// entries; lists of records (enemies, items...) as a spreadsheet, everything
// else as a typed tree. The selected record edits in full in the Inspector.


#include <cmath>
#include <map>
#include <tuple>

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

bool isTable(const Json& v) {
  return v.is_array() && !v.empty() && std::all_of(v.begin(), v.end(), [](const Json& e) { return e.is_object(); });
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

std::string pointerLabel(const Pointer& p) { return p.empty() ? "root" : p.to_string().substr(1); }

// Edits a scalar in place, frameless when `inCell`. True when changed.
bool scalarWidget(AssetDocument& doc, const Pointer& at, const Json& v, bool inCell, float width = -1) {
  const std::string key = at.to_string();
  ImGui::PushID(key.c_str());
  if (inCell) {
    ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::withAlpha(theme::bg2, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {4, 3});
  }
  ImGui::SetNextItemWidth(width);
  bool changed = false;
  const std::string label = "Edit " + pointerLabel(at);
  if (v.is_boolean()) {
    bool b = v.get<bool>();
    if (ui::toggle("##b", &b)) doc.edit(label, [&](Json& d) { d[at] = b; }), changed = true;
  } else if (v.is_number_integer()) {
    long long n = v.get<long long>();
    if (ImGui::DragScalar("##n", ImGuiDataType_S64, &n, 0.2f)) doc.edit(label, [&](Json& d) { d[at] = n; }, key), changed = true;
  } else if (v.is_number()) {
    double n = v.get<double>();
    if (ImGui::DragScalar("##f", ImGuiDataType_Double, &n, 0.01f, nullptr, nullptr, "%g")) doc.edit(label, [&](Json& d) { d[at] = n; }, key), changed = true;
  } else if (v.is_string() && !inCell && (v.get_ref<const std::string&>().size() > 32 || v.get_ref<const std::string&>().find('\n') != std::string::npos)) {
    // Prose (descriptions, dialogue) wraps in a box as tall as it needs.
    std::string s = v.get<std::string>();
    const float w = width < 0 ? ImGui::GetContentRegionAvail().x : width;
    const float textWidth = w - ImGui::GetStyle().FramePadding.x * 2;
    const float h = std::clamp(ImGui::CalcTextSize(s.c_str(), nullptr, false, textWidth).y, ImGui::GetTextLineHeight() * 2,
                               ImGui::GetTextLineHeight() * 10) + ImGui::GetStyle().FramePadding.y * 2 + 2;
    if (ImGui::InputTextMultiline("##s", &s, {w, h}, ImGuiInputTextFlags_WordWrap)) doc.edit(label, [&](Json& d) { d[at] = s; }, key), changed = true;
  } else if (v.is_string()) {
    // In a cell, typing replaces the value, as in a spreadsheet; a second click places the cursor.
    std::string s = v.get<std::string>();
    if (ImGui::InputText("##s", &s, inCell ? ImGuiInputTextFlags_AutoSelectAll : 0)) doc.edit(label, [&](Json& d) { d[at] = s; }, key), changed = true;
  } else {
    ImGui::TextColored(theme::textFaint, "null");
  }
  if (inCell) {
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
  }
  ImGui::PopID();
  return changed;
}

// Sprites: a text value naming an atlas region shows the picture, with the atlas's picker.
const Project* sProject = nullptr;  // the open project, for the duration of a draw

// The atlas defining `region`, else "".
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
  if (!sProject) return {};
  std::string lower = name;
  for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  bool pictured = false;
  for (const char* word : {"icon", "sprite", "portrait", "image", "frame", "picture"}) pictured |= lower.find(word) != std::string::npos;
  if (!pictured) return {};
  for (const AssetFile& f : sProject->files()) {
    if (f.kind == AssetKind::Atlas) return f.path;
  }
  return {};
}

void spriteWidget(AssetDocument& doc, const Pointer& at, const Json& v, const std::string& atlas, bool inCell, float width = -1) {
  const float fh = ImGui::GetFrameHeight();
  const std::string value = v.get<std::string>();
  ImGui::PushID(at.to_string().c_str());
  ImGui::BeginGroup();
  const ImVec2 a = ImGui::GetCursorScreenPos();
  ImGui::Dummy({fh, fh});
  if (auto p = Thumbnails::instance().get(*sProject, atlas + "#" + value)) {
    widgets::fitted(ImGui::GetWindowDrawList(), *p, {a.x + 1, a.y + 1}, {a.x + fh - 1, a.y + fh - 1});
  }
  ImGui::SameLine(0, 2);
  scalarWidget(doc, at, v, inCell, (width < 0 ? ImGui::GetContentRegionAvail().x : width - fh - 2) - fh);
  ImGui::SameLine(0, 0);
  ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, true);
  if (ui::iconButton("pickSprite", ICON_SQUARES_FOUR, "Pick a sprite", false, 0, fh)) ImGui::OpenPopup("sprites");
  ImGui::PopItemFlag();
  std::string picked = value;
  if (widgets::regionPopup("sprites", *sProject, atlas, picked)) doc.edit("Set " + pointerLabel(at), [&](Json& d) { d[at] = picked; });
  ImGui::EndGroup();
  ImGui::PopID();
}

// The menu every tree row has: change type, duplicate (in lists), delete.
void valueMenu(AssetDocument& doc, const Pointer& at, const Json& v, bool inArray) {
  if (!ImGui::BeginPopupContextItem("value")) return;
  if (ImGui::BeginMenu(ICON_SHAPES "  Change Type")) {
    const std::pair<const char*, Json> kTypes[] = {{"Text", ""}, {"Number", 0}, {"True / False", false},
                                                   {"List", Json::array()}, {"Group", Json::object()}};
    for (const auto& [name, blank] : kTypes) {
      if (ImGui::MenuItem(name)) doc.edit("Change Type of " + pointerLabel(at), [&](Json& d) { d[at] = blank; });
    }
    ImGui::EndMenu();
  }
  if (inArray && ImGui::MenuItem(ICON_COPY "  Duplicate")) {
    doc.edit("Duplicate " + pointerLabel(at), [&](Json& d) {
      Json& list = d[at.parent_pointer()];
      const size_t i = std::stoul(at.back());
      list.insert(list.begin() + static_cast<long>(i) + 1, v);
    });
  }
  if (!at.empty() && ImGui::MenuItem(ICON_TRASH "  Delete")) {
    doc.edit("Delete " + pointerLabel(at), [&](Json& d) {
      Json& parent = d[at.parent_pointer()];
      if (parent.is_array()) parent.erase(std::stoul(at.back()));
      else parent.erase(at.back());
    });
  }
  ImGui::EndPopup();
}

bool allPrimitive(const Json& v) {
  return v.is_array() && std::all_of(v.begin(), v.end(), [](const Json& e) { return e.is_primitive(); });
}

// A list of plain values as a row of chips: click one to edit it, x removes, + adds.
void chipList(AssetDocument& doc, const Pointer& at, const Json& list) {
  for (size_t i = 0; i < list.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    bool removed = false;
    if (ui::chip("item", summary(list[i]).c_str(), true, &removed)) ImGui::OpenPopup("editItem");
    if (removed) doc.edit("Remove from " + pointerLabel(at), [&](Json& d) { d[at].erase(i); });
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

// A typed tree: groups and lists fold; values edit in place.
void treeEditor(AssetDocument& doc, const Pointer& at, const Json& v, const std::string& label, bool inArray) {
  ImGui::PushID(at.to_string().c_str());
  if (allPrimitive(v)) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::textFaint, "%s", typeIcon(v));
    ImGui::SameLine(0, 6);
    ImGui::TextColored(inArray ? theme::textFaint : theme::textDim, "%s", label.c_str());
    valueMenu(doc, at, v, inArray);
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8, 170.0f));
    chipList(doc, at, v);
  } else if (v.is_object() || v.is_array()) {
    const bool open = ImGui::TreeNodeEx("##node", ImGuiTreeNodeFlags_SpanAvailWidth | (at.empty() || v.size() < 12 ? ImGuiTreeNodeFlags_DefaultOpen : 0),
                                        "%s  %s", typeIcon(v), label.c_str());
    valueMenu(doc, at, v, inArray);
    ImGui::SameLine();
    ImGui::TextColored(theme::textFaint, "%s", v.is_array() ? (std::to_string(v.size()) + " items").c_str() : summary(v).c_str());
    if (open) {
      if (v.is_object()) {
        for (const auto& [k, child] : v.items()) treeEditor(doc, at / k, child, k, false);
        static std::string newField;
        if (ui::button((std::string(ICON_PLUS "  Field##") + at.to_string()).c_str())) ImGui::OpenPopup("newField"), newField.clear();
        if (ImGui::BeginPopup("newField")) {
          if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
          ImGui::SetNextItemWidth(180);
          if (ImGui::InputTextWithHint("##name", "field name", &newField, ImGuiInputTextFlags_EnterReturnsTrue) && !newField.empty() &&
              !v.contains(newField)) {
            doc.edit("Add Field " + newField, [&](Json& d) { d[at][newField] = ""; });
            ImGui::CloseCurrentPopup();
          }
          ui::smallText("Enter adds it as text; right-click it to change its type.", theme::textFaint);
          ImGui::EndPopup();
        }
      } else {
        for (size_t i = 0; i < v.size(); ++i) treeEditor(doc, at / i, v[i], std::to_string(i), true);
        if (ui::button((std::string(ICON_PLUS "  Item##") + at.to_string()).c_str())) {
          doc.edit("Add Item to " + pointerLabel(at), [&](Json& d) { d[at].push_back(v.empty() ? Json("") : blankLike(v.back())); });
        }
      }
      ImGui::TreePop();
    }
  } else {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::textFaint, "%s", typeIcon(v));
    ImGui::SameLine(0, 6);
    ImGui::TextColored(inArray ? theme::textFaint : theme::textDim, "%s", label.c_str());
    valueMenu(doc, at, v, inArray);
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8, 170.0f));
    const std::string atlas = v.is_string() ? (v.get<std::string>().empty() ? atlasForName(label) : atlasOf(v.get<std::string>())) : "";
    if (!atlas.empty()) spriteWidget(doc, at, v, atlas, false);
    else scalarWidget(doc, at, v, false);
  }
  ImGui::PopID();
}

class DataEditor final : public AssetEditor {
 public:
  void draw(Editor& editor, AssetDocument& doc) override;
  bool drawInspector(Editor& editor, AssetDocument& doc) override;
  bool handles(const std::string& command) const override { return _row >= 0 && (command == "edit.duplicate" || command == "edit.delete"); }
  void run(const std::string& command, AssetDocument& doc) override {
    const Json& root = doc.value();
    const Pointer table = root.is_object() && root.contains(_section) ? Pointer() / _section : Pointer();
    const Json& rows = root[table];
    if (!isTable(rows) || _row >= static_cast<int>(rows.size())) return;
    const size_t r = static_cast<size_t>(_row);
    if (command == "edit.delete") {
      doc.edit("Delete Row", [&](Json& d) { d[table].erase(r); });
      _row = std::min(_row, static_cast<int>(rows.size()) - 1);  // `rows` is live: already one shorter
    } else {
      doc.edit("Duplicate Row", [&](Json& d) { d[table].insert(d[table].begin() + static_cast<long>(r) + 1, d[table][r]); });
      ++_row;
    }
  }

 private:
  std::string _section;     // top-level key shown (or "" for the root)
  int _row = -1;            // selected record in a table
  int _focusRow = -1;       // a row whose first cell takes the keyboard next frame
  std::string _filter;
  std::string _newColumn;
  int _newColumnKind = 0;     // Text, Number, True / False, List
  bool _refocusName = false;  // a type was clicked: back to the name, so Enter adds
  std::vector<std::string> _shownColumns;  // last frame's, to notice a reorder

  void drawTable(AssetDocument& doc, const Pointer& at, const Json& rows);
  void newColumnPopup(AssetDocument& doc, const Pointer& at, const std::vector<std::string>& columns);
};

void DataEditor::drawTable(AssetDocument& doc, const Pointer& at, const Json& rows) {
  // Columns: every key any record has, in first-seen order.
  std::vector<std::string> columns;
  for (const Json& row : rows) {
    for (const auto& [k, _] : row.items()) {
      if (std::find(columns.begin(), columns.end(), k) == columns.end()) columns.push_back(k);
    }
  }
  // Text columns that repeat a few values ("fire", "ice") offer them as choices.
  std::map<std::string, std::vector<std::string>> choices;
  for (const std::string& c : columns) {
    std::vector<std::string> values;
    size_t filled = 0;
    bool text = true;
    for (const Json& row : rows) {
      if (!row.contains(c)) continue;
      ++filled;
      if (!row[c].is_string()) text = false;
      else if (std::find(values.begin(), values.end(), row[c].get<std::string>()) == values.end()) values.push_back(row[c]);
    }
    if (text && values.size() >= 2 && values.size() <= 12 && values.size() < filled) choices[c] = values;
  }
  // Text columns naming sprites: the atlas they come from.
  std::map<std::string, std::string> spriteAtlas;
  for (const std::string& c : columns) {
    bool text = true, anyValue = false;
    std::string atlas;
    for (const Json& row : rows) {
      if (!row.contains(c)) continue;
      if (!row[c].is_string()) text = false;
      else if (!row[c].get<std::string>().empty() && atlas.empty()) anyValue = true, atlas = atlasOf(row[c]);
    }
    if (text && !anyValue) atlas = atlasForName(c);
    if (text && !atlas.empty()) spriteAtlas[c] = atlas;
  }
  const int count = static_cast<int>(columns.size()) + 2;
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {6, 3});
  const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInner | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
                                ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
  if (ImGui::BeginTable("##table", count, flags, {0, ImGui::GetContentRegionAvail().y - 40})) {
    // ImGui keeps a column where it was shown when its index changes; the records' key order is the truth.
    if (columns != _shownColumns) {
      if (!_shownColumns.empty()) ImGui::GetCurrentTable()->IsResetDisplayOrderRequest = true;
      _shownColumns = columns;
    }
    ImGui::TableSetupScrollFreeze(1, 1);
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
      const float width = wide ? std::clamp(typical + 40.0f, 150.0f, 340.0f) : 64.0f;
      ImGui::TableSetupColumn(c.c_str(), ImGuiTableColumnFlags_WidthFixed, width);
    }
    ImGui::TableSetupColumn("##add", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, ImGui::GetFrameHeight());
    // Header: names with a menu each, and a + to add a column.
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    ImGui::TableNextColumn();
    for (const std::string& c : columns) {
      ImGui::TableNextColumn();
      ImGui::PushID(c.c_str());
      ImGui::PushFont(theme::fonts().medium, theme::sizeSmall);
      ImGui::TableHeader(c.c_str());
      ImGui::PopFont();
      if (ImGui::BeginPopupContextItem("column")) {
        static std::string rename;
        if (ImGui::IsWindowAppearing()) rename = c, ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(160);
        if (ImGui::InputText("##rename", &rename, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll) && !rename.empty() &&
            rename != c) {
          doc.edit("Rename Column " + c, [&](Json& d) {
            for (Json& row : d[at]) {
              if (!row.contains(c)) continue;
              Json renamed = Json::object();
              for (auto& [k, val] : row.items()) renamed[k == c ? rename : k] = val;
              row = renamed;
            }
          });
          ImGui::CloseCurrentPopup();
        }
        if (ImGui::BeginMenu(ICON_SHAPES "  Change Type")) {
          // Every record's value converts: numbers to text and back, anything to a list of itself.
          static const char* kKinds[] = {"Text", "Number", "True / False", "List"};
          for (int k = 0; k < 4; ++k) {
            if (!ImGui::MenuItem(kKinds[k])) continue;
            doc.edit("Change Type of " + c, [&](Json& d) {
              for (Json& row : d[at]) {
                if (!row.contains(c)) continue;
                Json& v = row[c];
                if (k == 0) v = v.is_string() ? v : (v.is_primitive() && !v.is_null() ? Json(summary(v)) : Json(""));
                else if (k == 1) {
                  if (v.is_string()) {
                    const std::string t = v.get<std::string>();
                    char* end = nullptr;
                    const double n = std::strtod(t.c_str(), &end);
                    v = (end == t.c_str()) ? Json(0) : (n == std::floor(n) ? Json(static_cast<long long>(n)) : Json(n));
                  } else if (v.is_boolean()) v = v.get<bool>() ? 1 : 0;
                  else if (!v.is_number()) v = 0;
                } else if (k == 2) {
                  v = v.is_boolean() ? v : Json(v.is_number() ? v.get<double>() != 0 : (v.is_string() && (v == "true" || v == "yes")));
                } else if (!v.is_array()) {
                  v = (v.is_string() && v.get<std::string>().empty()) ? Json::array() : Json::array({v});
                }
              }
            });
          }
          ImGui::EndMenu();
        }
        // Columns are the records' key order; moving one reorders every record.
        const auto ci = std::find(columns.begin(), columns.end(), c) - columns.begin();
        for (const int dir : {-1, 1}) {
          const auto other = ci + dir;
          if (!ImGui::MenuItem(dir < 0 ? ICON_ARROW_LEFT "  Move Left" : ICON_ARROW_RIGHT "  Move Right", nullptr, false,
                               other >= 0 && other < static_cast<long>(columns.size()))) {
            continue;
          }
          std::vector<std::string> order = columns;
          std::swap(order[ci], order[other]);
          doc.edit("Move Column " + c, [&](Json& d) {
            for (Json& row : d[at]) {
              Json moved = Json::object();
              for (const std::string& k : order) {
                if (row.contains(k)) moved[k] = row[k];
              }
              row = moved;
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
      ImGui::PopID();
    }
    ImGui::TableNextColumn();
    if (ui::iconButton("addColumn", ICON_PLUS, "Add a column to every record")) ImGui::OpenPopup("newColumn"), _newColumn.clear(), _newColumnKind = 0;
    newColumnPopup(doc, at, columns);

    for (size_t r = 0; r < rows.size(); ++r) {
      const Json& row = rows[r];
      if (!_filter.empty() && ui::fuzzyScore(row.dump(), _filter) < 0) continue;
      ImGui::TableNextRow(0, ImGui::GetFrameHeight());
      ImGui::PushID(static_cast<int>(r));
      ImGui::TableNextColumn();
      const bool selected = _row == static_cast<int>(r);
      if (selected) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, theme::u32(theme::accent, 0.10f));
      ImGui::PushStyleColor(ImGuiCol_Text, selected ? theme::accent : theme::textFaint);
      // Tab walks the cells, not the row handles: a row reads like a form.
      ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, true);
      if (ImGui::Selectable(std::to_string(r + 1).c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                            {0, ImGui::GetFrameHeight()})) {
        _row = static_cast<int>(r);
      }
      ImGui::PopItemFlag();
      ImGui::PopStyleColor();
      if (ImGui::BeginPopupContextItem("row")) {
        _row = static_cast<int>(r);
        if (ImGui::MenuItem(ICON_COPY "  Duplicate")) doc.edit("Duplicate Row", [&](Json& d) { d[at].insert(d[at].begin() + static_cast<long>(r) + 1, row); });
        if (ImGui::MenuItem(ICON_ARROW_UP "  Move Up", nullptr, false, r > 0)) {
          doc.edit("Move Row", [&](Json& d) { std::swap(d[at][r], d[at][r - 1]); });
          _row = static_cast<int>(r) - 1;
        }
        if (ImGui::MenuItem(ICON_ARROW_DOWN "  Move Down", nullptr, false, r + 1 < rows.size())) {
          doc.edit("Move Row", [&](Json& d) { std::swap(d[at][r], d[at][r + 1]); });
          _row = static_cast<int>(r) + 1;
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_TRASH "  Delete Row")) doc.edit("Delete Row", [&](Json& d) { d[at].erase(r); });
        ImGui::EndPopup();
      }
      for (const std::string& c : columns) {
        ImGui::TableNextColumn();
        ImGui::PushID(c.c_str());
        // A new row: typing goes straight into its first cell.
        if (_focusRow == static_cast<int>(r) && c == columns.front() && row.contains(c) && row[c].is_primitive()) {
          ImGui::SetKeyboardFocusHere();
          _focusRow = -1;
        }
        ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, true);
        if (!row.contains(c)) {
          // A missing cell: click to give this record the column too.
          ImGui::PushStyleColor(ImGuiCol_Text, theme::textFaint);
          if (ImGui::Selectable("\xE2\x80\x94", false, ImGuiSelectableFlags_AllowOverlap)) {
            Json like = 0;
            for (const Json& other : rows) {
              if (other.contains(c)) {
                like = blankLike(other[c]);
                break;
              }
            }
            doc.edit("Add " + c, [&](Json& d) { d[at][r][c] = like; });
          }
          ImGui::PopStyleColor();
          ui::tooltip("Not set for this record. Click to add it.");
        } else if (row[c].is_string() && spriteAtlas.contains(c)) {
          ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, false);
          spriteWidget(doc, at / r / c, row[c], spriteAtlas[c], true);
          ImGui::PopItemFlag();
        } else if (row[c].is_string() && choices.contains(c)) {
          // Free text, with the column's values one click away.
          const float arrow = ImGui::GetFrameHeight();
          ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, false);
          scalarWidget(doc, at / r / c, row[c], true, ImGui::GetContentRegionAvail().x - arrow);
          ImGui::PopItemFlag();
          ImGui::SameLine(0, 0);
          if (ui::iconButton("pick", ICON_CARET_DOWN, nullptr, false, 0, arrow)) ImGui::OpenPopup("values");
          if (ImGui::BeginPopup("values")) {
            for (const std::string& v : choices[c]) {
              if (ImGui::Selectable(v.c_str(), row[c] == v)) doc.edit("Set " + c, [&](Json& d) { d[at / r / c] = v; });
            }
            ImGui::EndPopup();
          }
        } else if (row[c].is_primitive()) {
          ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, false);
          scalarWidget(doc, at / r / c, row[c], true);
          ImGui::PopItemFlag();
        } else {
          // Lists and groups read here; they edit in the Inspector.
          ImGui::PushStyleColor(ImGuiCol_Text, theme::textDim);
          if (ImGui::Selectable(ui::ellipsize(summary(row[c]), ImGui::GetContentRegionAvail().x).c_str(), false, ImGuiSelectableFlags_AllowOverlap)) {
            _row = static_cast<int>(r);
          }
          ImGui::PopStyleColor();
          ui::tooltip((summary(row[c]) + "\nEdit it in the Inspector").c_str());
        }
        ImGui::PopItemFlag();
        ImGui::PopID();
      }
      ImGui::TableNextColumn();
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::PopStyleVar();
  ImGui::Dummy({0, 4});
  if (ui::button(ICON_PLUS "  Add Row")) {
    _row = _focusRow = static_cast<int>(rows.size());  // before the edit: `rows` is the live document
    doc.edit("Add Row", [&](Json& d) { d[at].push_back(blankLike(rows.back())); });
    _filter.clear();
  }
  ImGui::SameLine();
  // Also here: on a wide table the header's + is scrolled out of sight.
  if (ui::button(ICON_PLUS "  Add Column")) ImGui::OpenPopup("newColumn"), _newColumn.clear(), _newColumnKind = 0;
  {
    std::vector<std::string> columns;
    for (const Json& row : rows) {
      for (const auto& [k, _] : row.items()) {
        if (std::find(columns.begin(), columns.end(), k) == columns.end()) columns.push_back(k);
      }
    }
    newColumnPopup(doc, at, columns);
  }
  ImGui::SameLine();
  ui::smallText("Right-click a row or column header for more. Lists and groups edit in the Inspector.", theme::textFaint);
}

// Names a new column and picks what it holds; every record gets a blank of that type.
void DataEditor::newColumnPopup(AssetDocument& doc, const Pointer& at, const std::vector<std::string>& columns) {
  if (ImGui::BeginPopup("newColumn")) {
    if (ImGui::IsWindowAppearing() || _refocusName) ImGui::SetKeyboardFocusHere(), _refocusName = false;
    ImGui::SetNextItemWidth(180);
    bool add = ImGui::InputTextWithHint("##name", "column name", &_newColumn, ImGuiInputTextFlags_EnterReturnsTrue);
    // What the column holds; a new record's cell starts blank of that type.
    static const std::tuple<const char*, const char*, Json> kKinds[] = {
        {ICON_TEXT_T, "Text", ""}, {ICON_HASH, "Number", 0}, {ICON_TOGGLE_LEFT, "True / False", false}, {ICON_LIST_BULLETS, "List", Json::array()}};
    for (int k = 0; k < 4; ++k) {
      ImGui::SameLine(0, k == 0 ? 8 : 2);
      const auto& [icon, name, _] = kKinds[k];
      if (ui::iconButton(name, icon, name, _newColumnKind == k)) _newColumnKind = k, _refocusName = true;  // Enter still adds
    }
    ImGui::SameLine(0, 8);
    add |= ui::primaryButton("Add");
    if (add && !_newColumn.empty() && std::find(columns.begin(), columns.end(), _newColumn) == columns.end()) {
      const Json blank = std::get<2>(kKinds[_newColumnKind]);
      doc.edit("Add Column " + _newColumn, [&](Json& d) {
        for (Json& row : d[at]) {
          if (!row.contains(_newColumn)) row[_newColumn] = blank;
        }
      });
      ImGui::CloseCurrentPopup();
    }
    if (ui::dismissPressed()) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

void DataEditor::draw(Editor& editor, AssetDocument& doc) {
  sProject = editor.project();
  const Json& root = doc.value();
  ui::beginDocumentBar(ICON_BRACKETS_CURLY, doc.title().c_str(), doc.path().c_str());
  ui::endDocumentBar();

  // An outline of the top-level entries when the file is a group of them.
  const bool outline = root.is_object() && !root.empty();
  if (outline && (_section.empty() || !root.contains(_section))) _section = root.begin().key();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10, 10});
  if (outline && root.size() > 1) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::bg1);
    ImGui::BeginChild("##outline", {200, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
    ui::sectionLabel("Contents");
    for (const auto& [k, v] : root.items()) {
      const std::string item = std::string(typeIcon(v)) + "  " + k;
      if (ImGui::Selectable(item.c_str(), k == _section, 0, {0, 26})) _section = k, _row = -1;
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 0);
  }
  ImGui::BeginChild("##body", {0, 0}, ImGuiChildFlags_AlwaysUseWindowPadding);
  const Pointer at = outline ? Pointer() / _section : Pointer();
  const Json& shown = outline ? root[_section] : root;
  if (isTable(shown)) {
    ImGui::PushFont(theme::fonts().semibold, theme::sizeTitle);
    ImGui::TextUnformatted(outline ? _section.c_str() : doc.title().c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextColored(theme::textFaint, "%zu records", shown.size());
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 200);
    ui::searchField("filter", _filter, "Find records", 200);
    ImGui::Dummy({0, 2});
    drawTable(doc, at, shown);
  } else {
    treeEditor(doc, at, shown, outline ? _section : doc.title(), false);
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
}

bool DataEditor::drawInspector(Editor& editor, AssetDocument& doc) {
  sProject = editor.project();
  const Json& root = doc.value();
  const bool outline = root.is_object() && root.contains(_section);
  const Pointer table = outline ? Pointer() / _section : Pointer();
  const Json& shown = outline ? root[_section] : root;
  if (!isTable(shown)) return false;
  if (_row < 0 || _row >= static_cast<int>(shown.size())) {
    ui::emptyState(ICON_TABLE, "Pick a record", "Select a row to edit all of its fields here, lists and groups included.");
    return true;
  }
  const Json& row = shown[static_cast<size_t>(_row)];
  // Title: the record's name-ish field, else its number.
  std::string title = "Record " + std::to_string(_row + 1);
  for (const char* key : {"name", "title", "label", "id"}) {
    if (row.contains(key) && row[key].is_string() && !row[key].get_ref<const std::string&>().empty()) {
      title = row[key];
      break;
    }
  }
  ImGui::PushFont(nullptr, 20.0f);
  ImGui::TextColored(theme::accent, ICON_TABLE);
  ImGui::PopFont();
  ImGui::SameLine(0, 10);
  ImGui::BeginGroup();
  ImGui::PushFont(theme::fonts().semibold, theme::sizeTitle);
  ImGui::TextUnformatted(title.c_str());
  ImGui::PopFont();
  ui::smallText((pointerLabel(table) + " #" + std::to_string(_row + 1)).c_str(), theme::textFaint);
  ImGui::EndGroup();
  ImGui::Dummy({0, 6});
  for (const auto& [k, v] : row.items()) treeEditor(doc, table / static_cast<size_t>(_row) / k, v, k, false);
  static std::string newField;
  if (ui::button(ICON_PLUS "  Field")) ImGui::OpenPopup("newRowField"), newField.clear();
  if (ImGui::BeginPopup("newRowField")) {
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(180);
    if (ImGui::InputTextWithHint("##name", "field name", &newField, ImGuiInputTextFlags_EnterReturnsTrue) && !newField.empty() &&
        !row.contains(newField)) {
      doc.edit("Add Field " + newField, [&](Json& d) { d[table / static_cast<size_t>(_row) / newField] = ""; });
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  return true;
}

}  // namespace

std::unique_ptr<AssetEditor> makeDataEditor() { return std::make_unique<DataEditor>(); }
