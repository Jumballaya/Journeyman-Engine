// The Inspector: the selected entity's components, edited through their
// schemas (FieldSchema), with prefab overrides, script params read from the
// script itself, asset pickers and an add-component search.

#include <cmath>
#include <utility>

#include <glm/trigonometric.hpp>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include "Entities.hpp"
#include "Icons.hpp"
#include "Panels.hpp"
#include "References.hpp"
#include "ScriptInfo.hpp"
#include "Theme.hpp"
#include "Thumbnails.hpp"
#include "Ui.hpp"
#include "audio/AudioModule.hpp"
#include "audio/SoundBuffer.hpp"
#include "TiledFiles.hpp"
#include "editors/EditorWidgets.hpp"

namespace {

using Kind = FieldSchema::Kind;

// One component of the selection being edited: each change goes to every
// selected entity; a merge key folds one drag into one undo step.
struct FieldContext {
  Editor& editor;
  const Json& entity;  // the primary selection
  EntityUid uid;
  std::string component;  // "SpriteComponent"
  std::string label;      // "Sprite"
  const ComponentSchema* schema;
  bool inherited;                              // the component comes from the prefab
  std::map<std::string, std::string>& drafts;  // text being typed, by field id
  std::string& pickerFilter;

  bool overridden(const std::string& key) const { return inherited && overrides(entity, component, key); }

  void edit(const std::string& what, const std::function<void(Json&)>& change, const std::string& mergeKey = {}) {
    editor.scene()->editEntities(editor.selection(), what, [&](Json& e) { change(editableComponent(e, component)); }, mergeKey);
  }

  // Sets the value at `path` inside the component; null erases it.
  void write(const std::vector<std::string>& path, const Json& value, const std::string& mergeKey = {}) {
    edit("Change " + label + " " + path.back(), [&](Json& c) { setAt(c, path, value); }, mergeKey);
  }

  // The same write, made later (once a new file has its name), to the entities selected now.
  std::function<void(const std::string&)> writeLater(std::vector<std::string> path) const {
    return [&editor = editor, targets = editor.selection(), component = component, label = label, path = std::move(path)](const std::string& value) {
      if (SceneDocument* scene = editor.scene()) {
        scene->editEntities(targets, "Change " + label + " " + path.back(), [&](Json& e) { setAt(editableComponent(e, component), path, value); });
      }
    };
  }

 private:
  static void setAt(Json& component, const std::vector<std::string>& path, const Json& value) {
    Json* at = &component;
    for (size_t i = 0; i + 1 < path.size(); ++i) {
      Json& next = (*at)[path[i]];
      if (!next.is_object()) next = Json::object();
      at = &next;
    }
    if (value.is_null()) at->erase(path.back());
    else (*at)[path.back()] = value;
  }
};

// "halfExtents" → "Half Extents"
std::string titleCase(const std::string& key) {
  std::string out;
  for (size_t i = 0; i < key.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(key[i]);
    if (i > 0 && std::isupper(c) && std::islower(static_cast<unsigned char>(key[i - 1]))) out += ' ';
    out += static_cast<char>(i == 0 ? std::toupper(c) : c);
  }
  return out;
}

float speedFor(const FieldSchema& f, float value) {
  if (f.step > 0) return f.step;
  return std::max(0.01f, std::abs(value) * 0.01f + 0.1f);
}

// The value a component starts with: each field's default.
Json defaults(const std::vector<FieldSchema>& fields) {
  Json out = Json::object();
  for (const FieldSchema& f : fields) {
    if (!f.defaultValue.is_null()) out[f.key] = f.defaultValue;
  }
  return out;
}

// Up to `n` numbers from a JSON array into `out`; anything else leaves `out` as is.
void readFloats(const Json& value, float* out, int n) {
  for (int i = 0; i < n && value.is_array() && i < static_cast<int>(value.size()); ++i) {
    if (value[i].is_number()) out[i] = value[i].get<float>();
  }
}

// ---- Asset fields -------------------------------------------------------------

// Every project path (and atlas region) a field may hold.
std::vector<std::string> candidatesFor(Editor& editor, const std::vector<std::string>& types) {
  std::vector<std::string> out;
  const Project& project = *editor.project();
  for (const AssetFile& f : project.files()) {
    if (f.kind == AssetKind::Folder) continue;
    if (assetMatches(f.path, types)) out.push_back(f.path);
    if (f.kind == AssetKind::Atlas && assetMatches(f.path + "#x", types)) {
      for (const std::string& region : Thumbnails::instance().regions(project, f.path)) out.push_back(f.path + "#" + region);
    }
  }
  return out;
}

// A square at `pos` with the reference's picture fitted in; false (just the square) when it has none.
bool drawPicture(const Project& project, const std::string& reference, ImVec2 pos, float side) {
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(pos, {pos.x + side, pos.y + side}, theme::u32(theme::bg1), theme::radius);
  auto picture = reference.empty() ? std::nullopt : Thumbnails::instance().get(project, reference);
  if (!picture) return false;
  const float fit = (side - 4) / std::max(picture->size.x, picture->size.y);
  const ImVec2 s{picture->size.x * fit, picture->size.y * fit};
  const ImVec2 a{pos.x + (side - s.x) * 0.5f, pos.y + (side - s.y) * 0.5f};
  draw->AddImage(picture->texture, a, {a.x + s.x, a.y + s.y}, picture->uv0, picture->uv1);
  return true;
}

// A reference's picture, or its kind's icon.
void drawAssetPicture(Editor& editor, const std::string& reference, ImVec2 pos, float side) {
  if (drawPicture(*editor.project(), reference, pos, side)) return;
  const char* icon = assetKindInfo(assetKindOf(reference.substr(0, reference.find('#')))).icon;
  const ImVec2 is = ImGui::CalcTextSize(icon);
  ImGui::GetWindowDrawList()->AddText({pos.x + (side - is.x) * 0.5f, pos.y + (side - is.y) * 0.5f},
                                      theme::u32(reference.empty() ? theme::textFaint : theme::textDim), icon);
}

// A field holding a project path: picture, name, a picker (which can also make a
// new file, handed to `assignNew` once named) and a drop target. Returns the new value when one is chosen.
std::optional<std::string> assetField(Editor& editor, const std::string& value, const std::vector<std::string>& types,
                                      std::string& filter, const std::function<void(const std::string&)>& assignNew) {
  std::optional<std::string> chosen;
  const float h = ImGui::GetFrameHeight();
  const float width = ImGui::GetContentRegionAvail().x;
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  if (ImGui::InvisibleButton("##field", {width - h - 4, h})) {
    filter.clear();
    ImGui::OpenPopup("picker");
  }
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(pos, {pos.x + width - h - 4, pos.y + h}, theme::u32(hovered ? theme::bg3 : theme::bg2), theme::radius);
  drawAssetPicture(editor, value, {pos.x + 2, pos.y + 2}, h - 4);
  const std::string shown = value.empty() ? "None" : value.substr(value.find_last_of("/#") + 1);
  draw->PushClipRect(pos, {pos.x + width - h - 10, pos.y + h}, true);
  draw->AddText({pos.x + h + 4, pos.y + (h - ImGui::GetTextLineHeight()) * 0.5f},
                theme::u32(value.empty() ? theme::textFaint : theme::text), shown.c_str());
  draw->PopClipRect();
  if (hovered && !value.empty()) ImGui::SetTooltip("%s", value.c_str());

  // Drop an asset from the Assets panel onto the field.
  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("JM_ASSET")) {
      const std::string dropped(static_cast<const char*>(p->Data), static_cast<size_t>(p->DataSize));
      if (assetMatches(dropped, types)) chosen = dropped;
      else editor.toasts().show(Toasts::Kind::Warning, "That file doesn't fit here", dropped);
    }
    ImGui::EndDragDropTarget();
  }
  ImGui::SameLine(0, 4);
  // Empty: make one (when a template can). Set: open it, in its editor when it has one, else in Assets.
  const auto kinds = Editor::newAssetKindsFor(types);
  if (value.empty() && !kinds.empty()) {
    const Editor::NewAssetKind& k = kinds.front();
    if (ui::iconButton("new", ICON_PLUS, (std::string(k.title) + "...").c_str(), false, 0, h)) editor.newAsset(k.kind, {}, assignNew);
  } else {
    const bool editable = value.find('#') == std::string::npos && Editor::hasAssetEditor(value);
    ImGui::BeginDisabled(value.empty());
    if (ui::iconButton("reveal", editable ? ICON_PENCIL_SIMPLE : ICON_ARROW_SQUARE_OUT,
                       editable ? (std::string("Edit ") + assetKindInfo(assetKindOf(value)).label).c_str() : "Show in Assets", false, 0, h)) {
      if (editable) editor.openAsset(value);
      else editor.revealAsset(value.substr(0, value.find('#')));
    }
    ImGui::EndDisabled();
  }

  ImGui::SetNextWindowSize({360, 420});
  if (ImGui::BeginPopup("picker")) {
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ui::searchField("search", filter, "Search");
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
    ImGui::Dummy({0, 2});
    ImGui::BeginChild("##options");
    for (const Editor::NewAssetKind& k : kinds) {
      if (ImGui::Selectable((std::string(ICON_PLUS "  ") + k.title + "...").c_str())) {
        ImGui::CloseCurrentPopup();
        editor.newAsset(k.kind, {}, assignNew);
      }
    }
    if (ImGui::Selectable(ICON_PROHIBIT "  None", value.empty())) chosen = std::string();
    // Best first: a match in the file's own name beats one strung across its folders.
    std::vector<std::pair<int, std::string>> ranked;
    for (const std::string& option : candidatesFor(editor, types)) {
      if (filter.empty()) {
        ranked.emplace_back(0, option);
        continue;
      }
      const int byName = ui::fuzzyScore(option.substr(option.find_last_of("/#") + 1), filter);
      const int byPath = ui::fuzzyScore(option, filter);
      if (byName >= 0) ranked.emplace_back(1000 + byName, option);
      else if (byPath >= 0) ranked.emplace_back(byPath, option);
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    if (enter && !filter.empty() && !ranked.empty()) chosen = ranked.front().second;  // typed it: Enter takes the top match
    for (const auto& [_, option] : ranked) {
      const ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::PushID(option.c_str());
      if (ImGui::Selectable("##o", option == value, 0, {0, 32})) chosen = option;
      ImGui::PopID();
      drawAssetPicture(editor, option, {p.x + 2, p.y + 1}, 30);
      const size_t cut = option.find_last_of("/#");
      ImGui::GetWindowDrawList()->AddText({p.x + 40, p.y + 1}, theme::u32(theme::text), option.substr(cut + 1).c_str());
      ImGui::PushFont(nullptr, theme::sizeSmall);
      ImGui::GetWindowDrawList()->AddText({p.x + 40, p.y + 17}, theme::u32(theme::textFaint), option.substr(0, cut).c_str());
      ImGui::PopFont();
    }
    if (ranked.empty()) ui::dimText("No matching files in the project.");
    ImGui::EndChild();
    if (chosen) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  return chosen;
}

// ---- Field editing ------------------------------------------------------------------

// One field row: label (with the override marker and a reset menu) and its widget.
void fieldRow(FieldContext& ctx, const FieldSchema& f, const Json& parent, std::vector<std::string> path, bool overridden) {
  const Json current = parent.contains(f.key) ? parent[f.key] : Json(f.defaultValue);
  path.push_back(f.key);
  std::string id = ctx.component;
  for (const std::string& p : path) id += "." + p;
  ui::propertyRow(titleCase(f.key).c_str(), f.hint.c_str(), overridden);

  if (f.kind == Kind::Group) {
    // A nested object that's off until enabled (like a sprite's shadow).
    bool on = current.is_object();
    if (ui::toggle(("##" + id).c_str(), &on)) ctx.write(path, on ? defaults(f.fields) : Json(nullptr));
    if (!current.is_object()) return;
    ImGui::Indent(12);
    for (const FieldSchema& sub : f.fields) fieldRow(ctx, sub, current, path, false);
    ImGui::Unindent(12);
    return;
  }

  if (ImGui::BeginPopupContextItem(("reset " + id).c_str())) {
    if (ImGui::MenuItem(overridden ? ICON_ARROW_U_UP_LEFT "  Revert to Prefab" : ICON_ARROW_COUNTER_CLOCKWISE "  Reset to Default")) {
      ctx.write(path, overridden || f.defaultValue.is_null() ? Json(nullptr) : Json(f.defaultValue));
    }
    ImGui::EndPopup();
  }
  ImGui::PushID(id.c_str());
  // Call right after the widget: one drag (or stretch of typing) is one undo step.
  const auto edited = [&](bool changed, const Json& value) {
    if (ImGui::IsItemActivated()) gestureKey(id, true);
    if (changed) ctx.write(path, value, gestureKey(id, false));
  };

  switch (f.kind) {
    case Kind::Number: {
      float v = current.is_number() ? current.get<float>() : 0.0f;
      const bool changed = ImGui::DragFloat("##v", &v, speedFor(f, v), f.min, f.max, "%.3g",
                                            f.max > f.min ? ImGuiSliderFlags_AlwaysClamp : 0);
      edited(changed, v);
      break;
    }
    case Kind::Integer: {
      int v = current.is_number() ? current.get<int>() : 0;
      const bool changed = ImGui::DragInt("##v", &v, 0.1f);
      edited(changed, v);
      break;
    }
    case Kind::Bool: {
      bool v = current.is_boolean() && current.get<bool>();
      if (ui::toggle("##v", &v)) ctx.write(path, v);
      break;
    }
    case Kind::Text: {
      std::string v = current.is_string() ? current.get<std::string>() : std::string();
      const bool changed = f.multiline ? ImGui::InputTextMultiline("##v", &v, {-FLT_MIN, ImGui::GetTextLineHeight() * 3 + 8})
                                       : ImGui::InputText("##v", &v);
      edited(changed, v);
      break;
    }
    case Kind::Vec2:
    case Kind::Vec3: {
      const int n = f.kind == Kind::Vec2 ? 2 : 3;
      float v[3] = {0, 0, 0};
      readFloats(current, v, n);
      const bool changed = ui::dragVector("##v", v, n, 0.5f, "%.4g");
      if (ImGui::IsMouseClicked(0) && ImGui::IsItemHovered()) gestureKey(id, true);
      edited(changed, n == 2 ? Json::array({v[0], v[1]}) : Json::array({v[0], v[1], v[2]}));
      break;
    }
    case Kind::Color: {
      if (current.is_null()) {
        // Optional colors (a text shadow) are off until set.
        if (ui::button(ICON_PLUS "  Add")) ctx.write(path, Json::array({0, 0, 0, 1}));
        break;
      }
      float c[4] = {1, 1, 1, 1};
      readFloats(current, c, 4);
      const bool changed = ui::colorField("##v", c);
      edited(changed, Json::array({c[0], c[1], c[2], c[3]}));
      break;
    }
    case Kind::Angle: {
      float degrees = glm::degrees(current.is_number() ? current.get<float>() : 0.0f);
      const bool changed = ImGui::DragFloat("##v", &degrees, 0.5f, 0, 0, "%.1f\xC2\xB0");
      edited(changed, glm::radians(degrees));
      break;
    }
    case Kind::Mask: {
      // 32 layer toggles in a popup; the button summarizes which are on.
      const uint32_t mask = current.is_number() ? current.get<uint32_t>() : 0;
      std::string summary;
      int count = 0;
      for (int bit = 0; bit < 32; ++bit) {
        if (!(mask & (1u << bit))) continue;
        if (++count <= 4) summary += (summary.empty() ? "" : ", ") + std::to_string(bit + 1);
      }
      if (mask == 0xFFFFFFFFu) summary = "All layers";
      else if (count == 0) summary = "None";
      else if (count > 4) summary += ", +" + std::to_string(count - 4);
      if (ImGui::Button((summary + "##mask").c_str(), {-FLT_MIN, 0})) ImGui::OpenPopup("layers");
      if (ImGui::BeginPopup("layers")) {
        ui::sectionLabel("Layers");
        uint32_t next = mask;
        for (int bit = 0; bit < 32; ++bit) {
          if (bit % 8) ImGui::SameLine();
          const bool on = next & (1u << bit);
          char text[8];
          std::snprintf(text, sizeof(text), "%2d", bit + 1);
          ImGui::PushStyleColor(ImGuiCol_Button, on ? theme::withAlpha(theme::accent, 0.85f) : theme::bg3);
          ImGui::PushStyleColor(ImGuiCol_Text, on ? theme::bg0 : theme::textDim);
          if (ImGui::Button(text, {30, 24})) next ^= (1u << bit);
          ImGui::PopStyleColor(2);
        }
        ImGui::Dummy({0, 4});
        if (ui::button("All")) next = 0xFFFFFFFFu;
        ImGui::SameLine();
        if (ui::button("None")) next = 0;
        if (next != mask) ctx.write(path, next);
        ImGui::EndPopup();
      }
      break;
    }
    case Kind::Choice: {
      const std::string v = current.is_string() ? current.get<std::string>() : std::string();
      if (ui::beginCombo("##v", v.c_str())) {
        for (const std::string& option : f.choices) {
          if (ImGui::Selectable(option.c_str(), option == v)) ctx.write(path, option);
        }
        ImGui::EndCombo();
      }
      break;
    }
    case Kind::Asset: {
      const std::string v = current.is_string() ? current.get<std::string>() : std::string();
      if (auto chosen = assetField(ctx.editor, v, f.assetTypes, ctx.pickerFilter, ctx.writeLater(path))) ctx.write(path, *chosen);
      break;
    }
    case Kind::StringMap: {
      const Json map = current.is_object() ? current : Json::object();
      Json next = map;
      std::string removeKey;
      for (auto it = map.begin(); it != map.end(); ++it) {
        ImGui::PushID(it.key().c_str());
        ImGui::TextColored(theme::textDim, "%s", it.key().c_str());
        ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.4f);
        std::string value = it->is_string() ? it->get<std::string>() : it->dump();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 4);
        if (ImGui::InputText("##value", &value)) next[it.key()] = value;
        if (ImGui::IsItemActivated()) gestureKey(id, true);
        ImGui::SameLine(0, 4);
        if (ui::iconButton("remove", ICON_MINUS, "Remove")) removeKey = it.key();
        ImGui::PopID();
      }
      if (!removeKey.empty()) next.erase(removeKey);
      std::string& draft = ctx.drafts[id + "+"];
      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 4);
      const bool add = ImGui::InputTextWithHint("##newkey", "Add a name...", &draft, ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::SameLine(0, 4);
      if ((add || ui::iconButton("add", ICON_PLUS, "Add")) && !draft.empty()) {
        next[draft] = "";
        draft.clear();
      }
      if (next != map) ctx.write(path, next, removeKey.empty() ? gestureKey(id, false) : std::string());
      break;
    }
    case Kind::Json: {
      // Edited as text; applied once it parses.
      std::string& draft = ctx.drafts[id];
      if (ImGui::GetActiveID() != ImGui::GetID("##json")) draft = current.is_null() ? std::string() : current.dump(2);
      const Json parsed = draft.empty() ? Json(nullptr) : Json::parse(draft, nullptr, false);
      const bool valid = !parsed.is_discarded();
      if (!valid) ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::withAlpha(theme::error, 0.18f));
      ImGui::PushFont(theme::fonts().mono, theme::sizeSmall + 0.5f);
      const int lines = std::clamp(static_cast<int>(std::count(draft.begin(), draft.end(), '\n')) + 1, 2, 12);
      ImGui::InputTextMultiline("##json", &draft, {-FLT_MIN, ImGui::GetTextLineHeight() * lines + 10}, ImGuiInputTextFlags_AllowTabInput);
      ImGui::PopFont();
      if (!valid) ImGui::PopStyleColor();
      edited(ImGui::IsItemEdited() && valid && parsed != current, parsed);
      if (!valid) ui::smallText(ICON_WARNING " Not valid JSON yet", theme::error);
      break;
    }
    case Kind::Group:
      break;
  }
  ImGui::PopID();
}

// The schema's rows for `component`, minus the keys a section draws itself.
void schemaRows(FieldContext& ctx, const Json& component, std::initializer_list<std::string_view> skip = {}) {
  for (const FieldSchema& f : ctx.schema->fields) {
    if (std::find(skip.begin(), skip.end(), f.key) == skip.end()) fieldRow(ctx, f, component, {}, ctx.overridden(f.key));
  }
}

// ---- Special sections ----------------------------------------------------------------

void scriptSection(FieldContext& ctx, const Json& component) {
  if (ui::beginProperties("script")) {
    schemaRows(ctx, component, {"params"});
    ui::endProperties();
  }
  const std::string script = component.value("script", std::string());
  if (script.empty()) return;

  // Params the script reads, then any others the scene sets.
  const Json params = component.value("params", Json::object());
  const auto& declared = scriptInfo(*ctx.editor.project(), script).params;
  ImGui::Dummy({0, 2});
  ui::sectionLabel("Params");
  if (declared.empty() && params.empty()) ui::smallText("This script reads no params.", theme::textFaint);
  if (ui::beginProperties("params")) {
    for (const ScriptInfo::Param& p : declared) {
      FieldSchema f = p.number ? FieldSchema::number(p.key, p.fallback.get<float>()) : FieldSchema::text(p.key, p.fallback.get<std::string>());
      f.hint = "Read by the script as Params." + std::string(p.number ? "number" : "text") + "(\"" + p.key + "\")";
      fieldRow(ctx, f, params, {"params"}, false);
    }
    for (auto it = params.begin(); it != params.end(); ++it) {
      if (std::any_of(declared.begin(), declared.end(), [&](const ScriptInfo::Param& p) { return p.key == it.key(); })) continue;
      FieldSchema f = it->is_number() ? FieldSchema::number(it.key(), 0) : it->is_boolean() ? FieldSchema::boolean(it.key(), false)
                                                                                           : FieldSchema::text(it.key(), "");
      f.hint = "Set in the scene; the script doesn't read it by this name";
      fieldRow(ctx, f, params, {"params"}, false);
    }
    ui::endProperties();
  }
  ImGui::Dummy({0, 2});
  if (ui::button(ICON_CODE "  Edit Script", {-FLT_MIN, 0})) ctx.editor.openInCodeEditor(script);
}

// Sprite animations as cards: a live preview, timing, and a strip of frames
// picked from the atlas.
void animationSection(FieldContext& ctx, const Json& component) {
  const Project& project = *ctx.editor.project();
  const std::string atlas = component.value("atlasPath", std::string());
  const std::string current = component.value("current", std::string());
  const Json animations = component.value("animations", Json::object());
  if (ui::beginProperties("anim")) {
    schemaRows(ctx, component, {"current", "animations"});
    // The starting animation, chosen from the ones defined below.
    ui::propertyRow("Plays First", "The animation playing when the entity spawns");
    if (ui::beginCombo("##current", current.empty() ? "None" : current.c_str())) {
      for (auto it = animations.begin(); it != animations.end(); ++it) {
        if (ImGui::Selectable(it.key().c_str(), it.key() == current)) ctx.write({"current"}, it.key());
      }
      ImGui::EndCombo();
    }
    ui::endProperties();
  }
  const auto regions = atlas.empty() ? std::vector<std::string>{} : Thumbnails::instance().regions(project, atlas);
  const auto drawRegion = [&](const std::string& region, ImVec2 at, float side) {
    if (!drawPicture(project, atlas + "#" + region, at, side)) {
      ImGui::GetWindowDrawList()->AddText({at.x + 4, at.y + 4}, theme::u32(theme::warning), ICON_WARNING);
    }
  };

  std::string removeAnimation;
  for (auto it = animations.begin(); it != animations.end(); ++it) {
    const std::string name = it.key();
    const Json& anim = it.value();
    const Json frames = anim.value("regions", Json::array());
    ImGui::PushID(name.c_str());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::withAlpha(theme::text, 0.03f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme::radius);
    ImGui::BeginChild("##card", {0, 0}, ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    // Live preview, cycling at the animation's own speed.
    const float duration = std::max(0.01f, anim.value("frameDuration", 0.1f));
    if (!frames.empty()) {
      const size_t frame = static_cast<size_t>(ImGui::GetTime() / duration) % frames.size();
      const ImVec2 at = ImGui::GetCursorScreenPos();
      ImGui::Dummy({40, 40});
      drawRegion(frames[frame].get<std::string>(), at, 40);
      ImGui::SameLine(0, 10);
    }
    ImGui::BeginGroup();
    std::string rename = name;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 6);
    ImGui::PushFont(theme::fonts().semibold, 0.0f);
    if (ImGui::InputText("##name", &rename, ImGuiInputTextFlags_EnterReturnsTrue) && !rename.empty() && rename != name &&
        !animations.contains(rename)) {
      Json next = Json::object();
      for (auto a = animations.begin(); a != animations.end(); ++a) next[a.key() == name ? rename : a.key()] = a.value();
      ctx.edit("Rename animation " + name, [&](Json& c) {
        c["animations"] = next;
        if (current == name) c["current"] = rename;
      });
    }
    ImGui::PopFont();
    ImGui::SameLine(0, 6);
    if (ui::iconButton("remove", ICON_TRASH, "Delete animation")) removeAnimation = name;
    float seconds = duration;
    ImGui::SetNextItemWidth(110);
    if (ImGui::DragFloat("##dur", &seconds, 0.005f, 0.01f, 5.0f, "%.3f s / frame", ImGuiSliderFlags_AlwaysClamp)) {
      ctx.write({"animations", name, "frameDuration"}, seconds, gestureKey("anim-dur-" + name, false));
    }
    if (ImGui::IsItemActivated()) gestureKey("anim-dur-" + name, true);
    ImGui::SameLine(0, 12);
    bool loop = anim.value("loop", true);
    if (ui::toggle("##loop", &loop)) ctx.write({"animations", name, "loop"}, loop);
    ImGui::SameLine(0, 6);
    ImGui::AlignTextToFramePadding();
    ui::dimText("Loop");
    ImGui::EndGroup();

    // Frames: click one to remove it; + adds from the atlas.
    const float side = 36.0f;
    std::optional<size_t> removeFrame;
    for (size_t i = 0; i < frames.size(); ++i) {
      if (i > 0) ImGui::SameLine(0, 4);
      if (ImGui::GetContentRegionAvail().x < side) ImGui::NewLine();
      const ImVec2 at = ImGui::GetCursorScreenPos();
      ImGui::PushID(static_cast<int>(i));
      if (ImGui::InvisibleButton("##frame", {side, side})) removeFrame = i;
      const bool hovered = ImGui::IsItemHovered();
      ImGui::PopID();
      drawRegion(frames[i].get<std::string>(), at, side);
      if (hovered) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(at, {at.x + side, at.y + side}, theme::u32(theme::error, 0.35f), theme::radius);
        draw->AddText({at.x + side * 0.5f - 6, at.y + side * 0.5f - 8}, theme::u32(theme::text), ICON_X);
        ImGui::SetTooltip("%s (click to remove)", frames[i].get<std::string>().c_str());
      }
    }
    if (!frames.empty()) ImGui::SameLine(0, 4);
    if (ImGui::GetContentRegionAvail().x < side) ImGui::NewLine();
    ImGui::BeginDisabled(regions.empty());
    if (ui::iconButton("addframe", ICON_PLUS, regions.empty() ? "Choose an atlas (and build) first" : "Add frames", false, 0, side)) {
      ctx.pickerFilter.clear();
      ImGui::OpenPopup("frames");
    }
    ImGui::EndDisabled();
    if (removeFrame) {
      Json next = frames;
      next.erase(next.begin() + static_cast<std::ptrdiff_t>(*removeFrame));
      ctx.write({"animations", name, "regions"}, next);
    }
    ImGui::SetNextWindowSize({320, 380});
    if (ImGui::BeginPopup("frames")) {
      if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
      ui::searchField("search", ctx.pickerFilter, "Search regions");
      ui::smallText("Click regions to append them in order.", theme::textFaint);
      ImGui::BeginChild("##regions");
      const float cell = 52.0f;
      const int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + 4) / (cell + 4)));
      int shown = 0;
      for (const std::string& region : regions) {
        if (!ctx.pickerFilter.empty() && ui::fuzzyScore(region, ctx.pickerFilter) < 0) continue;
        if (shown++ % columns) ImGui::SameLine(0, 4);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::PushID(region.c_str());
        if (ImGui::InvisibleButton("##r", {cell, cell})) {
          Json next = frames;
          next.push_back(region);
          ctx.write({"animations", name, "regions"}, next);
        }
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        drawRegion(region, at, cell);
        if (hovered) {
          ImGui::GetWindowDrawList()->AddRect(at, {at.x + cell, at.y + cell}, theme::u32(theme::accent), theme::radius, 1.5f);
          ImGui::SetTooltip("%s", region.c_str());
        }
      }
      ImGui::EndChild();
      ImGui::EndPopup();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::Dummy({0, 2});
    ImGui::PopID();
  }
  if (!removeAnimation.empty()) {
    Json next = animations;
    next.erase(removeAnimation);
    ctx.write({"animations"}, next);
  }
  if (ui::button(ICON_PLUS "  Add Animation", {-FLT_MIN, 0})) {
    std::string name = "anim";
    for (int n = 2; animations.contains(name); ++n) name = "anim" + std::to_string(n);
    ctx.edit("Add animation " + name, [&](Json& c) {
      c["animations"][name] = Json{{"regions", Json::array()}, {"frameDuration", 0.1}, {"loop", true}};
      if (animations.empty()) c["current"] = name;
    });
  }
}

// The map an entity draws: its file, layers (the active one is painted),
// tilesets, the selected object, and map-wide settings.
void tileMapSection(FieldContext& ctx, const Json& component) {
  Editor& editor = ctx.editor;
  if (ui::beginProperties("tilemap")) {
    schemaRows(ctx, component);
    ui::endProperties();
  }
  const Json source = component.value("map", Json());
  const std::string path = source.is_string() ? source.get<std::string>() : "";
  const Json* map = path.empty() ? nullptr : editor.map(path);
  if (!map) return;
  auto edit = [&](const std::string& label, const std::function<void(Json&)>& change, const std::string& key = {}) {
    editor.editMap(path, label, change, key);
  };
  const glm::ivec2 ts = tiled::tileSize(*map);
  char info[128];
  std::snprintf(info, sizeof(info), "%d x %d tiles of %d x %d px", tiled::width(*map), tiled::height(*map), ts.x, ts.y);
  ui::smallText(info, theme::textDim);
  if (const std::string problem = tiled::uneditable(*map); !problem.empty()) {
    ImGui::TextWrapped("%s", problem.c_str());
    if (ui::button(ICON_ARROW_SQUARE_OUT "  Open in Tiled", {-FLT_MIN, 0})) editor.openInTiled(path);
    return;
  }
  const float half = (ImGui::GetContentRegionAvail().x - 4) * 0.5f;
  if (ui::primaryButton(ICON_PAINT_BRUSH_BROAD "  Paint", {half, 0})) {
    editor.setTool(Tool::TileBrush);
    editor.focusPanel("Scene");
  }
  ImGui::SameLine(0, 4);
  if (ui::button(ICON_ARROW_SQUARE_OUT "  Open in Tiled", {-FLT_MIN, 0})) editor.openInTiled(path);

  // Layers, top first as they stack.
  const Json& layers = (*map)["layers"];
  int& active = editor.activeLayer(path);
  active = std::clamp(active, 0, std::max(0, static_cast<int>(layers.size()) - 1));
  ImGui::Dummy({0, 4});
  ui::sectionLabel("Layers");
  std::string& renaming = ctx.drafts["tilemap.rename"];
  for (int l = static_cast<int>(layers.size()) - 1; l >= 0; --l) {
    const Json& layer = layers[static_cast<size_t>(l)];
    ImGui::PushID(l);
    // Reordering and deleting show on the row under the mouse (and the active one), so the list reads as names.
    const float rowH = ImGui::GetFrameHeight();
    const ImVec2 rowMin = ImGui::GetCursorScreenPos();
    const bool rowHovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(rowMin, {rowMin.x + ImGui::GetContentRegionAvail().x, rowMin.y + rowH});
    const bool controls = rowHovered || l == active;
    const bool visible = layer.value("visible", true);
    if (ui::iconButton("eye", visible ? ICON_EYE : ICON_EYE_SLASH, visible ? "Hide (in the game too)" : "Show")) {
      edit(visible ? "Hide Layer" : "Show Layer", [&](Json& m) { m["layers"][static_cast<size_t>(l)]["visible"] = !visible; });
    }
    ImGui::SameLine(0, 4);
    const char* icon = tiled::isObjectLayer(layer) ? ICON_SHAPES : tiled::isTileLayer(layer) ? ICON_GRID_FOUR : layer.value("type", std::string()) == "group" ? ICON_STACK : ICON_IMAGE;
    const std::string name = layer.value("name", std::string());
    const float buttons = 3 * (rowH + 2);
    const std::string id = std::to_string(layer.value("id", 0));
    if (renaming == id) {
      std::string& draft = ctx.drafts["tilemap.renameText"];
      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttons);
      if (!ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
      if (ImGui::InputText("##rename", &draft, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
        const std::string to = draft;
        if (!to.empty()) edit("Rename Layer", [&](Json& m) { m["layers"][static_cast<size_t>(l)]["name"] = to; });
        renaming.clear();
      } else if (ImGui::IsItemDeactivated()) {
        renaming.clear();
      }
    } else {
      const std::string label = std::string(icon) + "  " + name;
      if (ImGui::Selectable(label.c_str(), l == active, ImGuiSelectableFlags_AllowDoubleClick, {ImGui::GetContentRegionAvail().x - buttons, rowH})) {
        if (tiled::isTileLayer(layer) || tiled::isObjectLayer(layer)) active = l, editor.selectedObject() = 0;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) renaming = id, ctx.drafts["tilemap.renameText"] = name;
      }
      ui::tooltip(tiled::isObjectLayer(layer) ? "Objects: shapes scripts find with map.objects()" : "Double-click to rename");
    }
    ImGui::SameLine(0, 2);
    if (!controls) {
      ImGui::Dummy({buttons - 2, rowH});
      ImGui::PopID();
      continue;
    }
    if (ui::iconButton("up", ICON_CARET_UP, "Move up") && l + 1 < static_cast<int>(layers.size())) {
      edit("Move Layer", [&](Json& m) { tiled::moveLayer(m, l, l + 1); });
      if (active == l) ++active;
    }
    ImGui::SameLine(0, 2);
    if (ui::iconButton("down", ICON_CARET_DOWN, "Move down") && l > 0) {
      edit("Move Layer", [&](Json& m) { tiled::moveLayer(m, l, l - 1); });
      if (active == l) --active;
    }
    ImGui::SameLine(0, 2);
    if (ui::iconButton("delete", ICON_TRASH, "Delete layer") && layers.size() > 1) {
      edit("Delete Layer", [&](Json& m) { m["layers"].erase(static_cast<size_t>(l)); });
      active = std::max(0, active - (active >= l ? 1 : 0));
    }
    ImGui::PopID();
  }
  if (ui::button(ICON_PLUS "  Tile Layer", {half, 0})) {
    int made = 0;
    edit("Add Tile Layer", [&](Json& m) { made = tiled::addLayer(m, "tilelayer", "Layer " + std::to_string(m["layers"].size() + 1), active); });
    active = made;
  }
  ImGui::SameLine(0, 4);
  if (ui::button(ICON_PLUS "  Object Layer", {-FLT_MIN, 0})) {
    int made = 0;
    edit("Add Object Layer", [&](Json& m) { made = tiled::addLayer(m, "objectgroup", "Objects", active); });
    active = made;
  }
  // The active layer's look.
  if (!layers.empty() && ui::beginProperties("layer", 96)) {
    const Json& layer = layers[static_cast<size_t>(active)];
    ui::propertyRow("Opacity");
    float opacity = layer.value("opacity", 1.0f);
    if (ImGui::SliderFloat("##opacity", &opacity, 0.0f, 1.0f, "%.2f")) {
      edit("Set Layer Opacity", [&](Json& m) { m["layers"][static_cast<size_t>(active)]["opacity"] = std::round(opacity * 100) / 100; }, "layerOpacity");
    }
    ui::propertyRow("Depth", "Added to the entity's z: a layer above the sprites (roofs, treetops) has a higher one. Unset: stacked in layer order.");
    const Json z = tiled::property(layer, "z");
    float depth = z.is_number() ? z.get<float>() : 0.0f;
    if (ImGui::DragFloat("##z", &depth, 0.05f, -100.0f, 100.0f, z.is_number() ? "%.2f" : "in order")) {
      edit("Set Layer Depth", [&](Json& m) { tiled::setProperty(m["layers"][static_cast<size_t>(active)], "z", depth); }, "layerDepth");
    }
    if (z.is_number()) {
      ImGui::SameLine(0, 4);
      if (ui::iconButton("unsetZ", ICON_X, "Back to layer order")) edit("Unset Layer Depth", [&](Json& m) { tiled::setProperty(m["layers"][static_cast<size_t>(active)], "z", nullptr); });
    }
    ui::endProperties();
  }

  // The selected object.
  int layerOf = -1;
  Json copy = *map;
  if (const Json* object = editor.selectedObject() ? tiled::findObject(copy, editor.selectedObject(), &layerOf) : nullptr) {
    const int id = editor.selectedObject();
    auto editObject = [&](const std::string& label, const std::function<void(Json&)>& change, const std::string& key) {
      edit(label, [&](Json& m) { if (Json* o = tiled::findObject(m, id)) change(*o); }, key);
    };
    ImGui::Dummy({0, 6});
    ui::sectionLabel("Object");
    if (ui::beginProperties("object", 96)) {
      ui::propertyRow("Name", "Scripts find it: map.object(\"name\")");
      std::string name = object->value("name", std::string());
      if (ImGui::InputText("##name", &name)) editObject("Rename Object", [&](Json& o) { o["name"] = name; }, "objectName");
      ui::propertyRow("Type", "Scripts find all of a type: map.objects(\"type\")");
      std::string type = object->value("type", object->value("class", std::string()));
      if (ImGui::InputText("##type", &type)) editObject("Set Object Type", [&](Json& o) { o.erase("class"), o["type"] = type; }, "objectType");
      const glm::vec4 r = tiled::objectRect(*map, *object);
      ui::propertyRow("Position", "Its bottom-left corner, in map pixels from the map's bottom-left");
      float at[2] = {r.x, r.y};
      if (ui::dragVector("##at", at, 2, 1.0f, "%.0f")) {
        editObject("Move Object", [&](Json& o) { tiled::setObjectRect(*map, o, {at[0], at[1], r.z, r.w}); }, "objectAt");
      }
      ui::propertyRow("Size", "Width and height in pixels; 0 x 0 is a point");
      float size[2] = {r.z, r.w};
      if (ui::dragVector("##size", size, 2, 1.0f, "%.0f")) {
        editObject("Resize Object", [&](Json& o) { tiled::setObjectRect(*map, o, {r.x, r.y, std::max(0.0f, size[0]), std::max(0.0f, size[1])}); }, "objectSize");
      }
      ui::endProperties();
    }
    widgets::properties(*object, ctx.drafts["tilemap.objectProperty"], [&](const std::string& label, const std::function<void(Json&)>& change, const std::string& key) {
      editObject(label, change, key);
    });
    if (ui::dangerButton(ICON_TRASH "  Delete Object", {-FLT_MIN, 0})) {
      edit("Delete Object", [&](Json& m) { tiled::removeObject(m, id); });
      editor.selectedObject() = 0;
    }
  }

  // Tilesets.
  ImGui::Dummy({0, 6});
  ui::sectionLabel("Tilesets");
  for (const tiled::TilesetRef& ref : tiled::tilesets(*map, path)) {
    if (ref.path.empty()) continue;
    ImGui::PushID(ref.path.c_str());
    if (ImGui::Selectable((std::string(ICON_GRID_FOUR "  ") + std::filesystem::path(ref.path).stem().string()).c_str(), false, 0,
                          {ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 4, ImGui::GetFrameHeight()})) {
      editor.openAsset(ref.path);
    }
    ui::tooltip(("Edit " + ref.path).c_str());
    ImGui::SameLine(0, 4);
    if (ui::iconButton("remove", ICON_X, "Remove from the map (its tiles are cleared)")) {
      edit("Remove Tileset", [&](Json& m) { tiled::removeTileset(m, path, ref.path); });
    }
    ImGui::PopID();
  }
  if (ui::beginCombo("##addTileset", ICON_PLUS "  Add Tileset")) {
    auto add = [&editor, path](const std::string& tileset) {
      editor.editMap(path, "Add Tileset", [&](Json& m) {
        tiled::addTileset(m, path, tileset, [&](const std::string& p) { return editor.tileset(p) ? tiled::tileSpan(*editor.tileset(p)) : 0u; });
      });
    };
    if (ImGui::Selectable(ICON_PLUS "  New Tileset...")) editor.newAsset("tileset", {}, add);
    for (const AssetFile& f : editor.project()->files()) {
      if (f.kind == AssetKind::Tileset && ImGui::Selectable(f.path.c_str())) add(f.path);
    }
    ImGui::EndCombo();
  }

  // Map-wide settings.
  ImGui::Dummy({0, 6});
  ui::sectionLabel("Map");
  if (ui::beginProperties("map", 96)) {
    ui::propertyRow("Size", "Columns and rows; the bottom-left corner stays put");
    int size[2] = {tiled::width(*map), tiled::height(*map)};
    if (ImGui::InputInt2("##size", size, ImGuiInputTextFlags_EnterReturnsTrue)) {
      edit("Resize Map", [&](Json& m) { tiled::resize(m, glm::clamp(glm::ivec2(size[0], size[1]), glm::ivec2(1), glm::ivec2(4096))); });
    }
    ui::propertyRow("Outside", "The tile type beyond the map's edges: a solid one keeps bodies in");
    const Json outside = tiled::property(*map, "outside");
    std::string value = outside.is_string() ? outside.get<std::string>() : "";
    if (ui::beginCombo("##outside", value.empty() ? "nothing" : value.c_str())) {
      if (ImGui::Selectable("nothing", value.empty())) edit("Set Outside", [&](Json& m) { tiled::setProperty(m, "outside", nullptr); });
      std::vector<std::string> types;
      for (const tiled::TilesetRef& ref : tiled::tilesets(*map, path)) {
        const Json* set = ref.path.empty() ? nullptr : editor.tileset(ref.path);
        for (uint32_t id : set ? tiled::tileIds(*set) : std::vector<uint32_t>{}) {
          const std::string t = tiled::typeOf(*set, id);
          if (!t.empty() && std::find(types.begin(), types.end(), t) == types.end()) types.push_back(t);
        }
      }
      for (const std::string& t : types) {
        if (ImGui::Selectable(t.c_str(), t == value)) edit("Set Outside", [&](Json& m) { tiled::setProperty(m, "outside", t); });
      }
      ImGui::EndCombo();
    }
    ui::endProperties();
  }
  widgets::properties(*map, ctx.drafts["tilemap.mapProperty"], [&](const std::string& label, const std::function<void(Json&)>& change, const std::string& key) {
    edit(label, change, key);
  }, {"outside"});
}

}  // namespace

// ---- The panel ------------------------------------------------------------------------

// An instance's link to its prefab: the prefab (click to show it), Edit, and
// an Overrides menu to apply or revert what this one changes.
void InspectorPanel::prefabBar(Editor& editor, EntityUid uid, const Json& entity) {
  const Project& project = *editor.project();
  const std::string prefab = entity.value("prefab", std::string());
  const bool found = prefabJson(project, prefab) != nullptr;
  // Position is where this instance stands, not a change to the prefab.
  auto overrides = overridesOf(entity);
  size_t changes = 0;
  for (auto& [component, keys] : overrides) {
    if (component == "TransformComponent") std::erase(keys, "position");
    changes += keys.size();
  }

  const float h = ImGui::GetFrameHeight() + 12;
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(pos, {pos.x + width, pos.y + h}, theme::u32(found ? theme::info : theme::error, 0.09f), theme::radius);
  draw->AddRect(pos, {pos.x + width, pos.y + h}, theme::u32(found ? theme::info : theme::error, 0.25f), theme::radius);

  // Thumbnail and name.
  const float pic = h - 10;
  const std::string image = prefabImage(project, prefab);
  if (auto p = image.empty() ? std::nullopt : Thumbnails::instance().get(project, image)) {
    const float fit = pic / std::max(p->size.x, p->size.y);
    const ImVec2 s{p->size.x * fit, p->size.y * fit};
    const ImVec2 at{pos.x + 5 + (pic - s.x) * 0.5f, pos.y + 5 + (pic - s.y) * 0.5f};
    draw->AddImage(p->texture, at, {at.x + s.x, at.y + s.y}, p->uv0, p->uv1);
  } else {
    const ImVec2 is = ImGui::CalcTextSize(ICON_CUBE);
    draw->AddText({pos.x + 5 + (pic - is.x) * 0.5f, pos.y + (h - is.y) * 0.5f}, theme::u32(theme::info), found ? ICON_CUBE : ICON_LINK_BREAK);
  }
  ImGui::SetCursorScreenPos({pos.x + pic + 12, pos.y});
  const std::string fileName = std::filesystem::path(prefab).stem().stem().string();
  if (ImGui::InvisibleButton("##prefabname", {std::max(10.0f, width - pic - 12 - 170), h})) editor.inspectAsset(prefab);
  const bool nameHovered = ImGui::IsItemHovered();
  ImGui::PushFont(theme::fonts().medium, 0.0f);
  draw->AddText({pos.x + pic + 12, pos.y + 6}, theme::u32(found ? (nameHovered ? theme::text : theme::info) : theme::error), fileName.c_str());
  ImGui::PopFont();
  ImGui::PushFont(nullptr, theme::sizeSmall);
  draw->AddText({pos.x + pic + 12, pos.y + h - ImGui::GetTextLineHeight() - 5}, theme::u32(theme::textFaint),
                found ? "Prefab instance" : "Prefab file missing");
  ImGui::PopFont();
  if (nameHovered) ImGui::SetTooltip("%s\nClick to see the prefab", prefab.c_str());

  // Overrides menu and Edit.
  ImGui::SetCursorScreenPos({pos.x + width - 166, pos.y + 6});
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {8, 3});
  char label[48];
  std::snprintf(label, sizeof(label), changes ? "Overrides %zu " ICON_CARET_DOWN : "No overrides " ICON_CARET_DOWN, changes);
  ImGui::PushStyleColor(ImGuiCol_Button, changes ? theme::bg3 : theme::withAlpha(theme::text, 0.06f));
  ImGui::PushStyleColor(ImGuiCol_Text, changes ? theme::accentBright : theme::textDim);
  if (ImGui::Button(label, {104, 0})) ImGui::OpenPopup("overrides");
  ImGui::PopStyleColor(2);
  ImGui::SameLine(0, 4);
  ImGui::BeginDisabled(!found);
  if (ImGui::Button(ICON_PENCIL_SIMPLE " Edit", {54, 0})) editor.editPrefab(prefab);
  ImGui::EndDisabled();
  ui::tooltip("Edit the prefab itself (every instance changes)");
  ImGui::PopStyleVar();
  ImGui::SetCursorScreenPos({pos.x, pos.y + h});
  ImGui::Dummy({0, 2});

  ImGui::SetNextWindowSizeConstraints({300, 0}, {420, 480});
  if (!ImGui::BeginPopup("overrides")) return;
  ui::heading("Overrides");
  ui::smallText("What this instance changes from the prefab.", theme::textDim);
  ImGui::Dummy({0, 4});
  if (changes == 0) ui::dimText("It matches the prefab.");
  for (const auto& [component, keys] : overrides) {
    if (keys.empty()) continue;
    ImGui::PushID(component.c_str());
    ImGui::TextColored(theme::accent, "%s", componentIcon(component));
    ImGui::SameLine(0, 8);
    ImGui::BeginGroup();
    ui::heading(componentLabel(component).c_str());
    std::string fields;
    for (const auto& key : keys) fields += (fields.empty() ? "" : ", ") + key;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 220);
    ui::smallText(fields.c_str(), theme::textDim);
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - 124);
    if (ImGui::SmallButton("Revert")) editor.revertOverrides(uid, component);
    ImGui::SameLine(0, 4);
    if (ImGui::SmallButton("Apply")) editor.applyOverrides(uid, component);
    ImGui::PopID();
    ImGui::Dummy({0, 2});
  }
  if (changes > 0) {
    ImGui::Separator();
    ImGui::Dummy({0, 2});
    if (ui::button(ICON_ARROW_U_UP_LEFT "  Revert All", {136, 0})) {
      editor.revertOverrides(uid);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine(0, 8);
    if (ui::primaryButton(ICON_UPLOAD_SIMPLE "  Apply All", {136, 0})) {
      editor.applyOverrides(uid);
      ImGui::CloseCurrentPopup();
    }
    ui::smallText("Apply writes them into the prefab for every instance.", theme::textFaint);
  }
  ImGui::EndPopup();
}

void InspectorPanel::draw(Editor& editor) {
  if (editor.playing() && editor.showLive()) {
    if (auto id = editor.liveSelection(); id && editor.game()->engine().getWorld().isAlive(*id)) {
      drawLive(editor, *id);
    } else {
      ui::emptyState(ICON_PLAY_CIRCLE, "The game is running", "Pick a running entity in the Hierarchy to see and tweak it live.");
    }
    return;
  }
  if (editor.drawAssetInspector()) return;
  SceneDocument* scene = editor.scene();
  if (editor.selection().empty() && !editor.inspectedAsset().empty() && editor.project()) {
    drawAsset(editor, editor.inspectedAsset());
    return;
  }
  if (!scene) {
    ui::emptyState(ICON_CURSOR_CLICK, "Nothing selected", "Open a scene, or pick a file in Assets to see it here.");
    return;
  }
  if (editor.selection().empty()) {
    drawSceneOverview(editor, *scene);
    return;
  }
  const Project& project = *editor.project();
  const EntityUid uid = editor.primary();
  const Json* found = scene->find(uid);
  if (!found) return;
  const Json entity = *found;  // a copy: edits below change the document
  const Json components = effectiveComponents(project, entity);
  const auto& targets = editor.selection();
  const bool isPrefab = entity.contains("prefab");
  // ImGui reapplies a field's last typing the frame after it loses focus, which can be
  // the frame a Scene click selected another entity (whose field has the same id).
  static EntityUid shown = 0;
  if (std::exchange(shown, uid) != uid) ImGui::GetCurrentContext()->InputTextDeactivatedState.ID = 0;

  // Header: icon, name, prefab link.
  ImGui::PushFont(nullptr, 18.0f);
  ImGui::TextColored(isPrefab ? theme::info : theme::accent, "%s", isPrefab ? ICON_CUBE : entityIcon(components));
  ImGui::PopFont();
  ImGui::SameLine(0, 8);
  const std::string entityName = entity.value("name", scene->isPrefab() ? scene->title() : std::string());
  // The field edits its own copy while focused (the entity's name would overwrite it every
  // frame), and the rename lands on Enter or on clicking away.
  static std::string editing;
  if (ImGui::GetActiveID() != ImGui::GetID("##name")) editing = entityName;
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::PushFont(theme::fonts().semibold, 0.0f);
  ImGui::BeginDisabled(scene->isPrefab());
  if ((ImGui::InputTextWithHint("##name", "Name", &editing, ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit()) &&
      editing != entityName) {
    const std::string renamed = editing;
    scene->editEntity(uid, "Rename to " + renamed, [&](Json& e) { e["name"] = renamed; });
  }
  ImGui::EndDisabled();
  ImGui::PopFont();
  if (scene->isPrefab()) {
    // Tags: chips with remove buttons, and a field to add one.
    const Json tags = entity.value("tags", Json::array());
    ImGui::Dummy({0, 2});
    ui::smallText("Tags", theme::textDim);
    std::string removeTag;
    for (const auto& tag : tags) {
      const std::string t = tag.get<std::string>();
      ImGui::PushID(t.c_str());
      ImGui::PushStyleColor(ImGuiCol_Button, theme::bg3);
      ImGui::PushStyleColor(ImGuiCol_Text, theme::text);
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {8, 2});
      if (ImGui::SmallButton((t + "  " ICON_X).c_str())) removeTag = t;
      ImGui::PopStyleVar();
      ImGui::PopStyleColor(2);
      ImGui::PopID();
      ImGui::SameLine(0, 4);
    }
    ImGui::SetNextItemWidth(std::max(90.0f, ImGui::GetContentRegionAvail().x));
    if (ImGui::InputTextWithHint("##addtag", "Add a tag...", &_newTag, ImGuiInputTextFlags_EnterReturnsTrue) && !_newTag.empty()) {
      scene->editEntity(uid, "Add tag " + _newTag, [&](Json& e) { e["tags"].push_back(_newTag); });
      _newTag.clear();
    }
    if (!removeTag.empty()) {
      scene->editEntity(uid, "Remove tag " + removeTag, [&](Json& e) {
        Json& list = e["tags"];
        list.erase(std::remove(list.begin(), list.end(), Json(removeTag)), list.end());
      });
    }
  }
  if (targets.size() > 1) {
    char multi[64];
    std::snprintf(multi, sizeof(multi), ICON_STACK "  Editing %zu entities", targets.size());
    ui::badge(multi, theme::accent);
  }
  if (isPrefab) prefabBar(editor, uid, entity);
  ImGui::Dummy({0, 4});

  // When it spawns: with the scene, with a group, or only under conditions.
  const std::string group = entity.value("group", std::string());
  const bool conditional = !group.empty() || entity.contains("if") || entity.contains("unless");
  if (!scene->isPrefab() &&
      ui::componentHeader("spawning", ICON_LIGHTNING, conditional ? "Spawning" : "Spawning: with the scene", {}, conditional)) {
    ImGui::Indent(4);
    if (ui::beginProperties("spawn")) {
      const auto setKey = [&](const char* key, const std::string& value, const std::string& label) {
        scene->editEntities(targets, label, [&](Json& e) {
          if (value.empty()) e.erase(key);
          else e[key] = value;
        });
      };
      ui::propertyRow("Group", "Held back until a script calls Scene.spawnGroup(name); despawning resets it");
      std::vector<std::string> groups;
      for (size_t i = 0; i < scene->size(); ++i) {
        const std::string g = scene->entity(i).value("group", std::string());
        if (!g.empty() && std::find(groups.begin(), groups.end(), g) == groups.end()) groups.push_back(g);
      }
      if (ui::beginCombo("##group", group.empty() ? "None (spawns with the scene)" : group.c_str())) {
        if (ImGui::Selectable("None (spawns with the scene)", group.empty())) setKey("group", "", "Remove from group");
        for (const std::string& g : groups) {
          if (ImGui::Selectable(g.c_str(), g == group)) setKey("group", g, "Move to group " + g);
        }
        ImGui::Separator();
        if (ImGui::Selectable(ICON_PLUS "  New group...")) {
          editor.prompt("New Group", "Group name (scripts spawn it with Scene.spawnGroup)", "room", [&editor](const std::string& g) {
            editor.scene()->editEntities(editor.selection(), "Move to group " + g, [&](Json& e) { e["group"] = g; });
          });
        }
        ImGui::EndCombo();
      }
      // A game-state key, committed on Enter or on clicking away.
      const auto conditionRow = [&](const char* key, const char* label, const char* hint) {
        ui::propertyRow(label, hint);
        std::string text = entity.value(key, std::string());
        if (ImGui::InputTextWithHint((std::string("##") + key).c_str(), "game-state key", &text, ImGuiInputTextFlags_EnterReturnsTrue) ||
            ImGui::IsItemDeactivatedAfterEdit()) {
          setKey(key, text, "Set spawn condition");
        }
      };
      conditionRow("if", "Only If", "A game-state key that must be set (true, nonzero) for this to spawn, e.g. done.boss");
      conditionRow("unless", "Unless", "A game-state key that stops this spawning once set, e.g. done.grove.12.4 for a collected key");
      ui::endProperties();
    }
    ImGui::Unindent(4);
    ImGui::Dummy({0, 4});
  }

  // Components, Transform first.
  std::vector<std::string> order;
  if (components.contains("TransformComponent")) order.push_back("TransformComponent");
  for (auto it = components.begin(); it != components.end(); ++it) {
    if (it.key() != "TransformComponent") order.push_back(it.key());
  }
  std::string removeComponent;
  for (const std::string& name : order) {
    const Json& component = components[name];
    FieldContext ctx{editor, entity, uid, name, componentLabel(name), componentSchema(name), fromPrefab(project, entity, name),
                     _jsonDrafts, _addFilter};
    ImGui::PushID(name.c_str());
    const bool open = ui::componentHeader(name.c_str(), componentIcon(name), ctx.label.c_str(), [&]() {
      if (ctx.inherited) {
        if (ImGui::MenuItem(ICON_ARROW_U_UP_LEFT "  Revert to Prefab", nullptr, false,
                            entity.contains("overrides") && entity["overrides"].contains(name))) {
          scene->editEntities(targets, "Revert " + ctx.label, [&](Json& e) {
            if (e.contains("overrides")) e["overrides"].erase(name);
          });
        }
      } else if (ImGui::MenuItem(ICON_ARROW_COUNTER_CLOCKWISE "  Reset") && ctx.schema) {
        ctx.edit("Reset " + ctx.label, [&](Json& c) { c = defaults(ctx.schema->fields); });
      }
      if (ImGui::MenuItem(ICON_COPY "  Copy as JSON")) ImGui::SetClipboardText(component.dump(2).c_str());
      if (ImGui::MenuItem(ICON_CLIPBOARD_TEXT "  Paste JSON")) {
        const Json pasted = Json::parse(ImGui::GetClipboardText() ? ImGui::GetClipboardText() : "", nullptr, false);
        if (pasted.is_object()) ctx.edit("Paste " + ctx.label, [&](Json& c) { c = pasted; });
        else editor.toasts().show(Toasts::Kind::Warning, "The clipboard has no JSON object");
      }
      ImGui::Separator();
      ImGui::BeginDisabled(ctx.inherited);
      if (ImGui::MenuItem(ICON_TRASH "  Remove Component")) removeComponent = name;
      ImGui::EndDisabled();
      if (ctx.inherited) ui::tooltip("Comes from the prefab");
    });
    if (open) {
      ImGui::Indent(4);
      if (!ctx.schema) {
        // No schema: each key edits as JSON.
        if (ui::beginProperties("raw")) {
          for (auto it = component.begin(); it != component.end(); ++it) {
            fieldRow(ctx, FieldSchema::json(it.key()), component, {}, ctx.overridden(it.key()));
          }
          ui::endProperties();
        }
      } else if (name == "ScriptComponent") {
        scriptSection(ctx, component);
      } else if (name == "SpriteAnimationComponent") {
        animationSection(ctx, component);
      } else if (name == "TileMapComponent") {
        tileMapSection(ctx, component);
      } else if (ui::beginProperties("fields")) {
        schemaRows(ctx, component);
        ui::endProperties();
      }
      if (name == "UIDocumentComponent" && !component.value("src", std::string()).empty()) {
        if (ui::button(ICON_BROWSER "  Edit Screen", {-FLT_MIN, 0})) editor.openAsset(component["src"]);
      }
      const std::string sound = name == "AudioEmitterComponent" ? component.value("sound", std::string()) : std::string();
      // Hear it as set: the sound at its volume, through the preview's mixer.
      if (!sound.empty() && ui::button(ICON_PLAY "  Preview Sound", {-FLT_MIN, 0})) {
        HostedEngine* preview = editor.preview().engine();
        if (AudioModule* audio = preview ? preview->engine().getModules().find<AudioModule>() : nullptr) {
          audio->audio().stopAll();
          if (!audio->audio().knows(sound)) audio->audio().registerSound({sound}, SoundBuffer::fromFile(project.abs(sound)));
          audio->audio().play(AudioHandle(sound), component.value("gain", 1.0f));
        }
      }
      ImGui::Unindent(4);
      ImGui::Dummy({0, 4});
    }
    ImGui::PopID();
  }
  if (!removeComponent.empty()) {
    scene->editEntities(targets, "Remove " + componentLabel(removeComponent), [&](Json& e) {
      if (e.contains("components")) e["components"].erase(removeComponent);
      if (e.contains("overrides")) e["overrides"].erase(removeComponent);
    });
  }

  // Add Component: a search over every component the engine knows.
  ImGui::Dummy({0, 6});
  if (ui::button(ICON_PLUS "  Add Component", {-FLT_MIN, 0})) {
    _addFilter.clear();
    ImGui::OpenPopup("add component");
  }
  ImGui::SetNextWindowSize({300, 360});
  if (ImGui::BeginPopup("add component")) {
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ui::searchField("search", _addFilter, "Search components");
    ImGui::Dummy({0, 2});
    std::vector<std::tuple<int, std::string, std::string>> matches;  // best score first, then by category and label
    for (const auto& [name, schema] : componentSchemas()) {
      if (components.contains(name)) continue;
      const int score = _addFilter.empty() ? 0 : ui::fuzzyScore(componentLabel(name) + " " + schema.category, _addFilter);
      if (score >= 0) matches.emplace_back(-score, schema.category + componentLabel(name), name);
    }
    std::sort(matches.begin(), matches.end());
    std::string category;
    bool first = true;
    for (const auto& [_, order, name] : matches) {
      const ComponentSchema* schema = componentSchema(name);
      if (_addFilter.empty() && schema->category != category) {
        category = schema->category;
        ui::sectionLabel(category.c_str());
      }
      const ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::PushID(name.c_str());
      const bool pick = ImGui::Selectable("##c", first && !_addFilter.empty(), 0, {0, 36}) ||
                        (first && !_addFilter.empty() && ImGui::IsKeyPressed(ImGuiKey_Enter));
      ImGui::PopID();
      first = false;
      ImDrawList* draw = ImGui::GetWindowDrawList();
      draw->AddText({p.x + 6, p.y + 9}, theme::u32(theme::accent), componentIcon(name));
      ImGui::PushFont(theme::fonts().medium, 0.0f);
      draw->AddText({p.x + 30, p.y + 1}, theme::u32(theme::text), componentLabel(name).c_str());
      ImGui::PopFont();
      ImGui::PushFont(nullptr, theme::sizeSmall);
      draw->AddText({p.x + 30, p.y + 19}, theme::u32(theme::textFaint), schema->summary.c_str());
      ImGui::PopFont();
      if (pick) {
        const Json initial = defaults(schema->fields);
        scene->editEntities(targets, "Add " + componentLabel(name), [&](Json& e) { editableComponent(e, name) = initial; });
        ImGui::CloseCurrentPopup();
      }
    }
    if (matches.empty()) ui::dimText("Nothing matches.");
    ImGui::EndPopup();
  }
  ImGui::Dummy({0, 12});
}

// With nothing selected: the scene itself. What's in it, its spawn groups,
// what loads it, and a way to play it.
void InspectorPanel::drawSceneOverview(Editor& editor, SceneDocument& scene) {
  const Project& project = *editor.project();
  ImGui::PushFont(nullptr, 20.0f);
  ImGui::TextColored(theme::accent, scene.isPrefab() ? ICON_CUBE : ICON_FILM_SLATE);
  ImGui::PopFont();
  ImGui::SameLine(0, 10);
  ImGui::BeginGroup();
  ImGui::PushFont(theme::fonts().semibold, theme::sizeTitle);
  ImGui::TextUnformatted(scene.title().c_str());
  ImGui::PopFont();
  ui::smallText(scene.path().c_str(), theme::textFaint);
  ImGui::EndGroup();
  ImGui::Dummy({0, 6});
  const float full = ImGui::GetContentRegionAvail().x;
  if (!scene.isPrefab()) {
    if (ui::primaryButton(ICON_PLAY "  Play This Scene", {full, 0})) editor.commands().run("play.scene");
    ImGui::Dummy({0, 6});
  }

  // Contents, by what things are.
  struct Kind {
    const char* icon;
    const char* one;
    int count = 0;
  };
  std::map<std::string, Kind> kinds;  // by plural label; the icon is the first one's
  std::map<std::string, int> groups;
  int conditional = 0;
  for (size_t i = 0; i < scene.size(); ++i) {
    const Json& e = scene.entity(i);
    const Json components = effectiveComponents(project, e);
    const char* icon = entityIcon(components);
    std::pair<const char*, const char*> label = {"entities", "entity"};
    if (components.contains("TileMapComponent")) label = {"tile maps", "tile map"};
    else if (components.contains("UIDocumentComponent")) label = {"UI screens", "UI screen"};
    else if (components.contains("TextComponent")) label = {"texts", "text"};
    else if (e.contains("prefab")) label = {"prefab instances", "prefab instance"}, icon = ICON_CUBE;
    ++kinds.try_emplace(label.first, Kind{icon, label.second}).first->second.count;
    if (e.contains("group")) ++groups[e["group"].get<std::string>()];
    if (e.contains("if") || e.contains("unless")) ++conditional;
  }
  ui::sectionLabel((std::to_string(scene.size()) + (scene.size() == 1 ? " entity" : " entities")).c_str());
  for (const auto& [label, k] : kinds) {
    ImGui::TextColored(theme::accent, "%s", k.icon);
    ImGui::SameLine(0, 8);
    ImGui::Text("%d  %s", k.count, k.count == 1 ? k.one : label.c_str());
  }
  if (!groups.empty() || conditional) {
    ImGui::Dummy({0, 6});
    ui::sectionLabel("Spawning");
    const auto line = [](const char* icon, const std::string& what, const std::string& note) {
      ImGui::TextColored(theme::textFaint, "%s", icon);
      ImGui::SameLine(0, 8);
      ImGui::TextUnformatted(what.c_str());
      ImGui::SameLine(0, 6);
      ImGui::TextColored(theme::textFaint, "%s", note.c_str());
    };
    for (const auto& [group, count] : groups) line(ICON_STACK, group, std::to_string(count) + ", when a script spawns the group");
    if (conditional) line(ICON_LIGHTNING, std::to_string(conditional), "only if (or unless) a game-state key is set");
  }
  // Who loads it.
  if (!scene.isPrefab()) {
    ImGui::Dummy({0, 6});
    auto users = referencesTo(project, scene.path());
    std::erase(users, ".jm.json");
    const bool first = editor.project()->manifest().value("entryScene", std::string()) == scene.path();
    if (first) {
      ImGui::TextColored(theme::accent, ICON_FLAG "  The game starts here");
      ImGui::Dummy({0, 4});
    }
    if (!first || !users.empty()) ui::sectionLabel(users.empty() ? "Nothing loads it by name" : "Loaded from");
    if (!project.inBuild(scene.path())) {
      ImGui::TextColored(theme::warning, ICON_WARNING "  Not in the game's scene list");
      if (ui::button("Add to the Game", {full, 0})) editor.addedFile(scene.path());
    }
    if (!first && users.empty()) ui::smallText("Scripts may still load it by a name they build at run time.", theme::textFaint);
    for (const std::string& u : users) {
      ImGui::PushID(u.c_str());
      if (ImGui::Selectable((std::string(assetKindInfo(assetKindOf(u)).icon) + "  " + u).c_str())) {
        if (assetKindOf(u) == AssetKind::Script) editor.openInCodeEditor(u);
        else editor.openAsset(u);
      }
      ImGui::PopID();
    }
    if (!first && project.inBuild(scene.path())) {
      // The first scene the game shows: usually a title screen.
      ImGui::Dummy({0, 4});
      if (ui::button(ICON_FLAG "  Start the Game Here", {full, 0})) {
        Project& owned = *editor.project();
        owned.manifest()["entryScene"] = scene.path();
        std::string error;
        if (owned.saveManifest(error)) editor.toasts().show(Toasts::Kind::Success, "The game starts at " + scene.title(), scene.path());
        else editor.toasts().show(Toasts::Kind::Error, "Couldn't update .jm.json", error);
      }
    }
  }
  ImGui::Dummy({0, 10});
  ui::smallText("Select an entity in the Scene or Hierarchy to edit it here.", theme::textFaint);
}
