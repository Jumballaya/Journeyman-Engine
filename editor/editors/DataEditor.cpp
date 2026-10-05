// The data editor (any other .json): an outline of the file's top-level
// entries; lists of records (enemies, items...) as a spreadsheet, everything
// else as a typed tree. The selected record edits in full in the Inspector.


#include <imgui.h>
#include <imgui_stdlib.h>

#include "AssetEditor.hpp"
#include "Editor.hpp"
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
bool scalarWidget(AssetDocument& doc, const Pointer& at, const Json& v, bool inCell) {
  const std::string key = at.to_string();
  ImGui::PushID(key.c_str());
  if (inCell) {
    ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::withAlpha(theme::bg2, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {4, 3});
  }
  ImGui::SetNextItemWidth(-1);
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
  } else if (v.is_string()) {
    std::string s = v.get<std::string>();
    if (ImGui::InputText("##s", &s)) doc.edit(label, [&](Json& d) { d[at] = s; }, key), changed = true;
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
    scalarWidget(doc, at, v, false);
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
      _row = std::min(_row, static_cast<int>(rows.size()) - 2);
    } else {
      doc.edit("Duplicate Row", [&](Json& d) { d[table].insert(d[table].begin() + static_cast<long>(r) + 1, d[table][r]); });
      ++_row;
    }
  }

 private:
  std::string _section;     // top-level key shown (or "" for the root)
  int _row = -1;            // selected record in a table
  std::string _filter;
  std::string _newColumn;

  void drawTable(AssetDocument& doc, const Pointer& at, const Json& rows);
};

void DataEditor::drawTable(AssetDocument& doc, const Pointer& at, const Json& rows) {
  // Columns: every key any record has, in first-seen order.
  std::vector<std::string> columns;
  for (const Json& row : rows) {
    for (const auto& [k, _] : row.items()) {
      if (std::find(columns.begin(), columns.end(), k) == columns.end()) columns.push_back(k);
    }
  }
  const int count = static_cast<int>(columns.size()) + 2;
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {6, 3});
  const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInner | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
                                ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
  if (ImGui::BeginTable("##table", count, flags, {0, ImGui::GetContentRegionAvail().y - 40})) {
    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 34);
    for (const std::string& c : columns) {
      bool wide = false;
      for (const Json& row : rows) wide |= row.contains(c) && (row[c].is_string() || !row[c].is_primitive());
      ImGui::TableSetupColumn(c.c_str(), ImGuiTableColumnFlags_WidthFixed, wide ? 150.0f : 64.0f);
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
        if (ImGui::IsWindowAppearing()) rename = c;
        ImGui::SetNextItemWidth(160);
        if (ImGui::InputText("##rename", &rename, ImGuiInputTextFlags_EnterReturnsTrue) && !rename.empty() && rename != c) {
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
    if (ui::iconButton("addColumn", ICON_PLUS, "Add a column to every record")) ImGui::OpenPopup("newColumn"), _newColumn.clear();
    if (ImGui::BeginPopup("newColumn")) {
      if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
      ImGui::SetNextItemWidth(180);
      if (ImGui::InputTextWithHint("##name", "column name", &_newColumn, ImGuiInputTextFlags_EnterReturnsTrue) && !_newColumn.empty()) {
        doc.edit("Add Column " + _newColumn, [&](Json& d) {
          for (Json& row : d[at]) {
            if (!row.contains(_newColumn)) row[_newColumn] = 0;
          }
        });
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    for (size_t r = 0; r < rows.size(); ++r) {
      const Json& row = rows[r];
      if (!_filter.empty() && ui::fuzzyScore(row.dump(), _filter) < 0) continue;
      ImGui::TableNextRow(0, ImGui::GetFrameHeight());
      ImGui::PushID(static_cast<int>(r));
      ImGui::TableNextColumn();
      const bool selected = _row == static_cast<int>(r);
      if (selected) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, theme::u32(theme::accent, 0.10f));
      ImGui::PushStyleColor(ImGuiCol_Text, selected ? theme::accent : theme::textFaint);
      if (ImGui::Selectable(std::to_string(r + 1).c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                            {0, ImGui::GetFrameHeight()})) {
        _row = static_cast<int>(r);
      }
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
        } else if (row[c].is_primitive()) {
          scalarWidget(doc, at / r / c, row[c], true);
        } else {
          // Lists and groups read here; they edit in the Inspector.
          ImGui::PushStyleColor(ImGuiCol_Text, theme::textDim);
          if (ImGui::Selectable(ui::ellipsize(summary(row[c]), ImGui::GetContentRegionAvail().x).c_str(), false, ImGuiSelectableFlags_AllowOverlap)) {
            _row = static_cast<int>(r);
          }
          ImGui::PopStyleColor();
          ui::tooltip((summary(row[c]) + "\nEdit it in the Inspector").c_str());
        }
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
    doc.edit("Add Row", [&](Json& d) { d[at].push_back(blankLike(rows.back())); });
    _row = static_cast<int>(rows.size());
  }
  ImGui::SameLine();
  ui::smallText("Right-click a row or column header for more. Lists and groups edit in the Inspector.", theme::textFaint);
}

void DataEditor::draw(Editor& editor, AssetDocument& doc) {
  (void)editor;
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
  (void)editor;
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
  for (const char* key : {"name", "id", "title", "label"}) {
    if (row.contains(key) && row[key].is_string()) title = row[key];
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
