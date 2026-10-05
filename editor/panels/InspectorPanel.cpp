// The Inspector: the selected entity's components, edited through their
// schemas (FieldSchema), with prefab overrides, script params read from the
// script itself, asset pickers and an add-component search.

#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>

#include <glm/gtc/constants.hpp>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include "Entities.hpp"
#include "Icons.hpp"
#include "Panels.hpp"
#include "Theme.hpp"
#include "Thumbnails.hpp"
#include "Ui.hpp"
#include "tilemap/TileGrid.hpp"

namespace {

using Kind = FieldSchema::Kind;

// How a field edit is written: the selected entities each get `value` at
// component/key; a gesture key folds one drag into one undo step.
struct FieldContext {
  Editor& editor;
  const std::vector<EntityUid>& targets;
  std::string component;
  std::string label;  // "Sprite"
};

void write(FieldContext& ctx, const std::vector<std::string>& path, const Json& value, const std::string& mergeKey = {}) {
  ctx.editor.scene()->editEntities(ctx.targets, "Change " + ctx.label + " " + path.back(), [&](Json& e) {
    Json* at = &editableComponent(e, ctx.component);
    for (size_t i = 0; i + 1 < path.size(); ++i) {
      Json& next = (*at)[path[i]];
      if (!next.is_object()) next = Json::object();
      at = &next;
    }
    if (value.is_null()) {
      at->erase(path.back());
    } else {
      (*at)[path.back()] = value;
    }
  }, mergeKey);
}

std::string titleCase(const std::string& key) {
  // "halfExtents" → "Half Extents"
  std::string out;
  for (size_t i = 0; i < key.size(); ++i) {
    const char c = key[i];
    if (i == 0) out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    else if (std::isupper(static_cast<unsigned char>(c)) && std::islower(static_cast<unsigned char>(key[i - 1]))) out += std::string(" ") + c;
    else out += c;
  }
  return out;
}

float speedFor(const FieldSchema& f, float value) {
  if (f.step > 0) return f.step;
  return std::max(0.01f, std::abs(value) * 0.01f + 0.1f);
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

// A picture (or icon) for a reference, drawn into a square at `pos`.
void drawAssetPicture(Editor& editor, const std::string& reference, ImVec2 pos, float side) {
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(pos, {pos.x + side, pos.y + side}, theme::u32(theme::bg1), theme::radius);
  if (auto picture = reference.empty() ? std::nullopt : Thumbnails::instance().get(*editor.project(), reference)) {
    const float fit = (side - 4) / std::max(picture->size.x, picture->size.y);
    const ImVec2 s{picture->size.x * fit, picture->size.y * fit};
    const ImVec2 a{pos.x + (side - s.x) * 0.5f, pos.y + (side - s.y) * 0.5f};
    draw->AddImage(picture->texture, a, {a.x + s.x, a.y + s.y}, picture->uv0, picture->uv1);
    return;
  }
  const char* icon = assetKindInfo(assetKindOf(reference.substr(0, reference.find('#')))).icon;
  const ImVec2 is = ImGui::CalcTextSize(icon);
  draw->AddText({pos.x + (side - is.x) * 0.5f, pos.y + (side - is.y) * 0.5f},
                theme::u32(reference.empty() ? theme::textFaint : theme::textDim), icon);
}

// A field holding a project path: picture, name, a picker and a drop target.
// Returns the new value when one is chosen.
std::optional<std::string> assetField(Editor& editor, const char* id, const std::string& value,
                                      const std::vector<std::string>& types, std::string& filter) {
  std::optional<std::string> chosen;
  ImGui::PushID(id);
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
  ImGui::BeginDisabled(value.empty());
  if (ui::iconButton("reveal", ICON_ARROW_SQUARE_OUT, "Show in Assets", false, 0, h)) {
    editor.revealAsset(value.substr(0, value.find('#')));
  }
  ImGui::EndDisabled();

  ImGui::SetNextWindowSize({360, 420});
  if (ImGui::BeginPopup("picker")) {
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ui::searchField("search", filter, "Search");
    ImGui::Dummy({0, 2});
    ImGui::BeginChild("##options");
    if (ImGui::Selectable(ICON_PROHIBIT "  None", value.empty())) chosen = std::string();
    const auto options = candidatesFor(editor, types);
    for (const std::string& option : options) {
      if (!filter.empty() && ui::fuzzyScore(option, filter) < 0) continue;
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
    if (options.empty()) ui::dimText("No matching files in the project.");
    ImGui::EndChild();
    if (chosen) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  ImGui::PopID();
  return chosen;
}

// ---- Script params ---------------------------------------------------------------

struct ScriptParam {
  std::string key;
  bool number = true;
  Json fallback;
};

// The params a script reads (Params.number("speed", 150), Params.text("theme")),
// so the inspector can offer them with their types and defaults.
std::vector<ScriptParam> scriptParams(const Project& project, const std::string& script) {
  static std::map<std::string, std::pair<std::filesystem::file_time_type, std::vector<ScriptParam>>> cache;
  std::error_code ec;
  const auto modified = std::filesystem::last_write_time(project.abs(script), ec);
  if (ec) return {};
  auto& entry = cache[script];
  if (entry.first == modified && !entry.second.empty()) return entry.second;
  static const std::regex call(R"re(Params\.(number|text)\(\s*"([A-Za-z0-9_]+)"\s*(?:,\s*([^),]+))?)re");
  std::vector<ScriptParam> params;
  const std::string source = project.readText(script);
  for (auto it = std::sregex_iterator(source.begin(), source.end(), call); it != std::sregex_iterator(); ++it) {
    const std::string key = (*it)[2];
    if (std::any_of(params.begin(), params.end(), [&](const ScriptParam& p) { return p.key == key; })) continue;
    ScriptParam p{key, (*it)[1] == "number", nullptr};
    const std::string fallback = (*it)[3].matched ? std::string((*it)[3]) : std::string();
    if (p.number) {
      char* end = nullptr;
      const double v = std::strtod(fallback.c_str(), &end);
      p.fallback = fallback.empty() || end == fallback.c_str() ? Json(0) : Json(v);
    } else {
      p.fallback = fallback.size() >= 2 && fallback.front() == '"' ? Json(fallback.substr(1, fallback.size() - 2)) : Json("");
    }
    params.push_back(std::move(p));
  }
  entry = {modified, params};
  return params;
}

}  // namespace

// ---- Field editing ------------------------------------------------------------------

namespace {

// One field row: label (with modified marker and reset menu) and its widget.
void fieldRow(FieldContext& ctx, const FieldSchema& f, const Json& componentJson, const std::vector<std::string>& path,
              bool overridden, std::map<std::string, std::string>& drafts, std::string& pickerFilter) {
  const Json current = componentJson.contains(f.key) ? componentJson[f.key] : Json(f.defaultValue);
  // The accent marker means "overrides the prefab"; resetting a plain value is in the label's menu.
  const bool modified = overridden;
  const std::string label = titleCase(f.key);
  const std::string id = ctx.component + "." + [&] {
    std::string joined;
    for (const auto& p : path) joined += p + ".";
    return joined;
  }();
  std::vector<std::string> fieldPath = path;
  fieldPath.push_back(f.key);

  if (f.kind == Kind::Group) {
    // A nested object that's off until enabled (like a sprite's shadow).
    const bool on = componentJson.contains(f.key) && componentJson[f.key].is_object();
    ui::propertyRow(label.c_str(), f.hint.c_str(), modified);
    bool enabled = on;
    if (ui::toggle(("##" + id + f.key).c_str(), &enabled)) {
      Json initial = Json::object();
      for (const FieldSchema& sub : f.fields) {
        if (!sub.defaultValue.is_null()) initial[sub.key] = sub.defaultValue;
      }
      write(ctx, fieldPath, enabled ? initial : Json(nullptr));
    }
    if (on) {
      ImGui::Indent(12);
      for (const FieldSchema& sub : f.fields) fieldRow(ctx, sub, componentJson[f.key], fieldPath, false, drafts, pickerFilter);
      ImGui::Unindent(12);
    }
    return;
  }

  ui::propertyRow(label.c_str(), f.hint.c_str(), modified);
  if (ImGui::BeginPopupContextItem(("reset " + id + f.key).c_str())) {
    if (ImGui::MenuItem(overridden ? ICON_ARROW_U_UP_LEFT "  Revert to Prefab" : ICON_ARROW_COUNTER_CLOCKWISE "  Reset to Default")) {
      write(ctx, fieldPath, overridden || f.defaultValue.is_null() ? Json(nullptr) : Json(f.defaultValue));
    }
    ImGui::EndPopup();
  }
  ImGui::PushID((id + f.key).c_str());
  const auto gesture = [&](bool activated) { return gestureKey(id + f.key, activated); };

  switch (f.kind) {
    case Kind::Number: {
      float v = current.is_number() ? current.get<float>() : 0.0f;
      const bool bounded = f.max > f.min;
      if (ImGui::DragFloat("##v", &v, speedFor(f, v), f.min, f.max, "%.3g", bounded ? ImGuiSliderFlags_AlwaysClamp : 0)) {
        write(ctx, fieldPath, v, gesture(false));
      }
      if (ImGui::IsItemActivated()) gesture(true);
      break;
    }
    case Kind::Integer: {
      int v = current.is_number() ? current.get<int>() : 0;
      if (ImGui::DragInt("##v", &v, 0.1f)) write(ctx, fieldPath, v, gesture(false));
      if (ImGui::IsItemActivated()) gesture(true);
      break;
    }
    case Kind::Bool: {
      bool v = current.is_boolean() && current.get<bool>();
      if (ui::toggle("##v", &v)) write(ctx, fieldPath, v);
      break;
    }
    case Kind::Text: {
      std::string v = current.is_string() ? current.get<std::string>() : std::string();
      const bool changed = f.multiline ? ImGui::InputTextMultiline("##v", &v, {-FLT_MIN, ImGui::GetTextLineHeight() * 3 + 8})
                                       : ImGui::InputText("##v", &v);
      if (ImGui::IsItemActivated()) gesture(true);
      if (changed) write(ctx, fieldPath, v, gesture(false));
      break;
    }
    case Kind::Vec2:
    case Kind::Vec3: {
      const int n = f.kind == Kind::Vec2 ? 2 : 3;
      float v[3] = {0, 0, 0};
      for (int i = 0; i < n && current.is_array() && i < static_cast<int>(current.size()); ++i) v[i] = current[i].get<float>();
      if (ui::dragVector("##v", v, n, 0.5f, "%.4g")) {
        write(ctx, fieldPath, n == 2 ? Json::array({v[0], v[1]}) : Json::array({v[0], v[1], v[2]}), gesture(false));
      }
      if (ImGui::IsItemActivated() || (ImGui::IsMouseClicked(0) && ImGui::IsItemHovered())) gesture(true);
      break;
    }
    case Kind::Color: {
      if (current.is_null()) {
        // Optional colors (a text shadow) are off until set.
        if (ui::button(ICON_PLUS "  Add")) write(ctx, fieldPath, Json::array({0, 0, 0, 1}));
        break;
      }
      float c[4] = {1, 1, 1, 1};
      for (int i = 0; i < 4 && current.is_array() && i < static_cast<int>(current.size()); ++i) c[i] = current[i].get<float>();
      if (ImGui::ColorEdit4("##v", c, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_DisplayHex |
                                         ImGuiColorEditFlags_Float)) {
        write(ctx, fieldPath, Json::array({c[0], c[1], c[2], c[3]}), gesture(false));
      }
      if (ImGui::IsItemActivated()) gesture(true);
      break;
    }
    case Kind::Angle: {
      float degrees = (current.is_number() ? current.get<float>() : 0.0f) * 180.0f / glm::pi<float>();
      if (ImGui::DragFloat("##v", &degrees, 0.5f, 0, 0, "%.1f\xC2\xB0")) {
        write(ctx, fieldPath, degrees * glm::pi<float>() / 180.0f, gesture(false));
      }
      if (ImGui::IsItemActivated()) gesture(true);
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
          bool on = next & (1u << bit);
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
        if (next != mask) write(ctx, fieldPath, next);
        ImGui::EndPopup();
      }
      break;
    }
    case Kind::Choice: {
      const std::string v = current.is_string() ? current.get<std::string>() : std::string();
      if (ui::beginCombo("##v", v.c_str())) {
        for (const std::string& option : f.choices) {
          if (ImGui::Selectable(option.c_str(), option == v)) write(ctx, fieldPath, option);
        }
        ImGui::EndCombo();
      }
      break;
    }
    case Kind::Asset: {
      const std::string v = current.is_string() ? current.get<std::string>() : std::string();
      if (auto chosen = assetField(ctx.editor, "asset", v, f.assetTypes, pickerFilter)) write(ctx, fieldPath, *chosen);
      break;
    }
    case Kind::StringMap: {
      const Json map = current.is_object() ? current : Json::object();
      Json next = map;
      std::string removeKey;
      for (auto it = map.begin(); it != map.end(); ++it) {
        ImGui::PushID(it.key().c_str());
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.4f);
        ImGui::TextColored(theme::textDim, "%s", it.key().c_str());
        ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.4f);
        std::string value = it->is_string() ? it->get<std::string>() : it->dump();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 4);
        if (ImGui::InputText("##value", &value)) next[it.key()] = value;
        if (ImGui::IsItemActivated()) gesture(true);
        ImGui::SameLine(0, 4);
        if (ui::iconButton("remove", ICON_MINUS, "Remove")) removeKey = it.key();
        ImGui::PopID();
      }
      if (!removeKey.empty()) next.erase(removeKey);
      std::string& draft = drafts[id + f.key + "+"];
      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 4);
      const bool add = ImGui::InputTextWithHint("##newkey", "Add a name...", &draft, ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::SameLine(0, 4);
      if ((add || ui::iconButton("add", ICON_PLUS, "Add")) && !draft.empty()) {
        next[draft] = "";
        draft.clear();
      }
      if (next != map) write(ctx, fieldPath, next, removeKey.empty() ? gesture(false) : std::string());
      break;
    }
    case Kind::Json: {
      // Edited as text; applied once it parses.
      std::string& draft = drafts[id + f.key];
      const std::string text = current.is_null() ? std::string() : current.dump(2);
      ImGuiID inputId = ImGui::GetID("##json");
      const bool active = ImGui::GetActiveID() == inputId;
      if (!active) draft = text;
      const Json parsed = draft.empty() ? Json(nullptr) : Json::parse(draft, nullptr, false);
      const bool valid = !parsed.is_discarded();
      if (!valid) ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::withAlpha(theme::error, 0.18f));
      ImGui::PushFont(theme::fonts().mono, theme::sizeSmall + 0.5f);
      const int lines = std::clamp(static_cast<int>(std::count(draft.begin(), draft.end(), '\n')) + 1, 2, 12);
      ImGui::InputTextMultiline("##json", &draft, {-FLT_MIN, ImGui::GetTextLineHeight() * lines + 10}, ImGuiInputTextFlags_AllowTabInput);
      ImGui::PopFont();
      if (!valid) ImGui::PopStyleColor();
      if (ImGui::IsItemActivated()) gesture(true);
      if (ImGui::IsItemEdited() && valid && parsed != current) write(ctx, fieldPath, parsed, gesture(false));
      if (!valid) ui::smallText(ICON_WARNING " Not valid JSON yet", theme::error);
      break;
    }
    case Kind::Group:
      break;
  }
  ImGui::PopID();
}

}  // namespace

// ---- Special sections ----------------------------------------------------------------

namespace {

void scriptSection(Editor& editor, FieldContext& ctx, const Json& component, const Json& entity,
                   std::map<std::string, std::string>& drafts, std::string& pickerFilter) {
  const Project& project = *editor.project();
  const ComponentSchema* schema = componentSchema("ScriptComponent");
  const std::string script = component.value("script", std::string());
  if (ui::beginProperties("script")) {
    for (const FieldSchema& f : schema->fields) {
      if (f.key == "params") continue;
      fieldRow(ctx, f, component, {}, overrides(entity, "ScriptComponent", f.key), drafts, pickerFilter);
    }
    ui::endProperties();
  }
  if (script.empty()) return;

  // Params the script reads, then any others the scene sets.
  const Json params = component.value("params", Json::object());
  const auto declared = scriptParams(project, script);
  ImGui::Dummy({0, 2});
  ui::sectionLabel("Params");
  if (declared.empty() && params.empty()) ui::smallText("This script reads no params.", theme::textFaint);
  if (ui::beginProperties("params")) {
    for (const ScriptParam& p : declared) {
      FieldSchema f = p.number ? FieldSchema::number(p.key, p.fallback.get<float>()) : FieldSchema::text(p.key, p.fallback.get<std::string>());
      f.hint = "Read by the script as Params." + std::string(p.number ? "number" : "text") + "(\"" + p.key + "\")";
      fieldRow(ctx, f, params, {"params"}, false, drafts, pickerFilter);
    }
    for (auto it = params.begin(); it != params.end(); ++it) {
      if (std::any_of(declared.begin(), declared.end(), [&](const ScriptParam& p) { return p.key == it.key(); })) continue;
      FieldSchema f = it->is_number() ? FieldSchema::number(it.key(), 0) : it->is_boolean() ? FieldSchema::boolean(it.key(), false)
                                                                                           : FieldSchema::text(it.key(), "");
      f.hint = "Set in the scene; the script doesn't read it by this name";
      fieldRow(ctx, f, params, {"params"}, false, drafts, pickerFilter);
    }
    ui::endProperties();
  }
  ImGui::Dummy({0, 2});
  if (ui::button(ICON_CODE "  Edit Script", {-FLT_MIN, 0})) editor.openInCodeEditor(script);
}

// Sprite animations as cards: a live preview, timing, and a strip of frames
// picked from the atlas.
void animationSection(Editor& editor, FieldContext& ctx, const Json& component, const Json& entity,
                      std::map<std::string, std::string>& drafts, std::string& pickerFilter) {
  const Project& project = *editor.project();
  const ComponentSchema* schema = componentSchema("SpriteAnimationComponent");
  const std::string atlas = component.value("atlasPath", std::string());
  const Json animations = component.value("animations", Json::object());
  if (ui::beginProperties("anim")) {
    for (const FieldSchema& f : schema->fields) {
      if (f.key != "atlasPath") continue;
      fieldRow(ctx, f, component, {}, overrides(entity, "SpriteAnimationComponent", f.key), drafts, pickerFilter);
    }
    // The starting animation, chosen from the ones defined below.
    ui::propertyRow("Plays First", "The animation playing when the entity spawns");
    const std::string current = component.value("current", std::string());
    if (ui::beginCombo("##current", current.empty() ? "None" : current.c_str())) {
      for (auto it = animations.begin(); it != animations.end(); ++it) {
        if (ImGui::Selectable(it.key().c_str(), it.key() == current)) write(ctx, {"current"}, it.key());
      }
      ImGui::EndCombo();
    }
    ui::endProperties();
  }
  const auto regions = atlas.empty() ? std::vector<std::string>{} : Thumbnails::instance().regions(project, atlas);
  auto drawRegion = [&](const std::string& region, ImVec2 at, float side) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(at, {at.x + side, at.y + side}, theme::u32(theme::bg1), theme::radius);
    if (auto p = Thumbnails::instance().get(project, atlas + "#" + region)) {
      const float fit = (side - 4) / std::max(p->size.x, p->size.y);
      const ImVec2 s{p->size.x * fit, p->size.y * fit};
      draw->AddImage(p->texture, {at.x + (side - s.x) * 0.5f, at.y + (side - s.y) * 0.5f},
                     {at.x + (side + s.x) * 0.5f, at.y + (side + s.y) * 0.5f}, p->uv0, p->uv1);
    } else {
      draw->AddText({at.x + 4, at.y + 4}, theme::u32(theme::warning), ICON_WARNING);
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
      write(ctx, {"animations"}, next);
      if (component.value("current", std::string()) == name) write(ctx, {"current"}, rename);
    }
    ImGui::PopFont();
    ImGui::SameLine(0, 6);
    if (ui::iconButton("remove", ICON_TRASH, "Delete animation")) removeAnimation = name;
    float seconds = duration;
    ImGui::SetNextItemWidth(110);
    if (ImGui::DragFloat("##dur", &seconds, 0.005f, 0.01f, 5.0f, "%.3f s / frame", ImGuiSliderFlags_AlwaysClamp)) {
      write(ctx, {"animations", name, "frameDuration"}, seconds, gestureKey("anim-dur-" + name, false));
    }
    if (ImGui::IsItemActivated()) gestureKey("anim-dur-" + name, true);
    ImGui::SameLine(0, 12);
    bool loop = anim.value("loop", true);
    if (ui::toggle("##loop", &loop)) write(ctx, {"animations", name, "loop"}, loop);
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
      pickerFilter.clear();
      ImGui::OpenPopup("frames");
    }
    ImGui::EndDisabled();
    if (removeFrame) {
      Json next = frames;
      next.erase(next.begin() + static_cast<std::ptrdiff_t>(*removeFrame));
      write(ctx, {"animations", name, "regions"}, next);
    }
    ImGui::SetNextWindowSize({320, 380});
    if (ImGui::BeginPopup("frames")) {
      if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
      ui::searchField("search", pickerFilter, "Search regions");
      ui::smallText("Click regions to append them in order.", theme::textFaint);
      ImGui::BeginChild("##regions");
      const float cell = 52.0f;
      const int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + 4) / (cell + 4)));
      int shown = 0;
      for (const std::string& region : regions) {
        if (!pickerFilter.empty() && ui::fuzzyScore(region, pickerFilter) < 0) continue;
        if (shown++ % columns) ImGui::SameLine(0, 4);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::PushID(region.c_str());
        if (ImGui::InvisibleButton("##r", {cell, cell})) {
          Json next = frames;
          next.push_back(region);
          write(ctx, {"animations", name, "regions"}, next);
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
    write(ctx, {"animations"}, next);
  }
  if (ui::button(ICON_PLUS "  Add Animation", {-FLT_MIN, 0})) {
    std::string name = "anim";
    for (int n = 2; animations.contains(name); ++n) name = "anim" + std::to_string(n);
    write(ctx, {"animations", name}, Json{{"regions", Json::array()}, {"frameDuration", 0.1}, {"loop", true}});
    if (animations.empty()) write(ctx, {"current"}, name);
  }
}

void tileMapSection(Editor& editor, FieldContext& ctx, const Json& component, const Json& entity, EntityUid uid,
                    std::map<std::string, std::string>& drafts, std::string& pickerFilter) {
  const TileGrid* grid = editor.preview().tileGrid(uid);
  if (grid) {
    char info[96];
    std::snprintf(info, sizeof(info), "%d x %d tiles  ·  %.0f px", grid->width(), grid->height(), grid->tileSize());
    ui::smallText(info, theme::textDim);
  }
  const Json rows = component.value("rows", Json());
  if (ui::primaryButton(ICON_PAINT_BRUSH_BROAD "  Paint Tiles", {-FLT_MIN, 0})) {
    editor.setTool(Tool::TileBrush);
    editor.focusPanel("Scene");
  }
  ImGui::Dummy({0, 2});
  if (ui::beginProperties("tilemap")) {
    for (const FieldSchema& f : componentSchema("TileMapComponent")->fields) {
      if (f.key == "rows") continue;
      fieldRow(ctx, f, component, {}, overrides(entity, "TileMapComponent", f.key), drafts, pickerFilter);
    }
    // Rows: a .txt file, or kept in the scene.
    ui::propertyRow("Rows", "Where the map's characters live");
    if (rows.is_string()) {
      ImGui::TextColored(theme::textDim, ICON_FILE_TEXT " %s", rows.get<std::string>().c_str());
    } else {
      ImGui::TextColored(theme::textDim, ICON_LIST " In the scene (%zu rows)", rows.is_array() ? rows.size() : 0);
    }
    // Resize, keeping the bottom-left corner where it is.
    if (grid) {
      ui::propertyRow("Size", "Columns and rows; tiles past the new edge are dropped");
      int size[2] = {grid->width(), grid->height()};
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::InputInt2("##size", size, ImGuiInputTextFlags_EnterReturnsTrue)) {
        size[0] = std::clamp(size[0], 1, 1024);
        size[1] = std::clamp(size[1], 1, 1024);
        std::vector<std::string> current = editor.mapRows(uid);
        std::vector<std::string> next(static_cast<size_t>(size[1]), std::string(static_cast<size_t>(size[0]), ' '));
        // Rows are top first; align bottoms so the map grows upward.
        for (int y = 0; y < size[1]; ++y) {
          const int from = static_cast<int>(current.size()) - size[1] + y;
          if (from < 0 || from >= static_cast<int>(current.size())) continue;
          for (int x = 0; x < size[0] && x < static_cast<int>(current[from].size()); ++x) next[y][x] = current[from][x];
        }
        editor.setMapRows(uid, next, "Resize map", {});
      }
    }
    ui::endProperties();
  }
}

}  // namespace

// ---- The panel ------------------------------------------------------------------------

// An instance's link to its prefab: the prefab (click to show it), Edit, and
// an Overrides menu to apply or revert what this one changes.
void InspectorPanel::prefabBar(Editor& editor, EntityUid uid, const Json& entity) {
  const Project& project = *editor.project();
  const std::string prefab = entity.value("prefab", std::string());
  const bool found = prefabJson(project, prefab) != nullptr;
  const auto overrides = overridesOf(entity);
  // Position is where this instance stands, not a change to the prefab.
  size_t changes = 0;
  for (const auto& [component, keys] : overrides) {
    for (const auto& key : keys) changes += !(component == "TransformComponent" && key == "position");
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
  ImGui::PushStyleColor(ImGuiCol_Button, changes ? theme::withAlpha(theme::accent, 0.22f) : theme::withAlpha(theme::text, 0.06f));
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
  if (ImGui::BeginPopup("overrides")) {
    ui::heading("Overrides");
    ui::smallText("What this instance changes from the prefab.", theme::textDim);
    ImGui::Dummy({0, 4});
    if (changes == 0) ui::dimText("It matches the prefab.");
    for (const auto& [component, keys] : overrides) {
      std::vector<std::string> shown;
      for (const auto& key : keys) {
        if (!(component == "TransformComponent" && key == "position")) shown.push_back(key);
      }
      if (shown.empty()) continue;
      ImGui::PushID(component.c_str());
      ImGui::TextColored(theme::accent, "%s", componentIcon(component));
      ImGui::SameLine(0, 8);
      ImGui::BeginGroup();
      ui::heading(componentLabel(component).c_str());
      std::string fields;
      for (const auto& key : shown) fields += (fields.empty() ? "" : ", ") + key;
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
}

void InspectorPanel::draw(Editor& editor) {
  SceneDocument* scene = editor.scene();
  if (editor.selection().empty() && !editor.inspectedAsset().empty() && editor.project()) {
    drawAsset(editor, editor.inspectedAsset());
    return;
  }
  if (!scene || editor.selection().empty()) {
    ui::emptyState(ICON_CURSOR_CLICK, "Nothing selected", "Select an entity in the Scene or Hierarchy to edit it here.");
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

  // Header: icon, name, prefab link.
  {
    ImGui::PushFont(nullptr, 18.0f);
    ImGui::TextColored(isPrefab ? theme::info : theme::accent, "%s", isPrefab ? ICON_CUBE : entityIcon(components));
    ImGui::PopFont();
    ImGui::SameLine(0, 8);
    std::string name = entity.value("name", scene->isPrefab() ? scene->title() : std::string());
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::PushFont(theme::fonts().semibold, 0.0f);
    ImGui::BeginDisabled(scene->isPrefab());
    if (ImGui::InputTextWithHint("##name", "Name", &name, ImGuiInputTextFlags_EnterReturnsTrue) ||
        (ImGui::IsItemDeactivatedAfterEdit())) {
      scene->editEntity(uid, "Rename to " + name, [&](Json& e) { e["name"] = name; });
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
        ImGui::PushStyleColor(ImGuiCol_Button, theme::withAlpha(theme::accent, 0.16f));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::accentBright);
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
  }

  // When it spawns: with the scene, with a group, or only under conditions.
  if (!scene->isPrefab()) {
    const std::string group = entity.value("group", std::string());
    const std::string ifKey = entity.value("if", std::string());
    const std::string unlessKey = entity.value("unless", std::string());
    const bool anySet = !group.empty() || !ifKey.empty() || !unlessKey.empty();
    if (ui::componentHeader("spawning", ICON_LIGHTNING, anySet ? "Spawning" : "Spawning: with the scene", {}, anySet)) {
      ImGui::Indent(4);
      if (ui::beginProperties("spawn")) {
        auto setKey = [&](const char* key, const std::string& value, const std::string& label) {
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
        std::string ifText = ifKey, unlessText = unlessKey;
        ui::propertyRow("Only If", "A game-state key that must be set (true, nonzero) for this to spawn, e.g. done.boss");
        if (ImGui::InputTextWithHint("##if", "game-state key", &ifText, ImGuiInputTextFlags_EnterReturnsTrue) ||
            ImGui::IsItemDeactivatedAfterEdit()) {
          setKey("if", ifText, "Set spawn condition");
        }
        ui::propertyRow("Unless", "A game-state key that stops this spawning once set, e.g. done.grove.12.4 for a collected key");
        if (ImGui::InputTextWithHint("##unless", "game-state key", &unlessText, ImGuiInputTextFlags_EnterReturnsTrue) ||
            ImGui::IsItemDeactivatedAfterEdit()) {
          setKey("unless", unlessText, "Set spawn condition");
        }
        ui::endProperties();
      }
      ImGui::Unindent(4);
      ImGui::Dummy({0, 4});
    }
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
    const ComponentSchema* schema = componentSchema(name);
    const std::string label = componentLabel(name);
    const bool inherited = fromPrefab(project, entity, name);
    FieldContext ctx{editor, targets, name, label};
    ImGui::PushID(name.c_str());
    const bool open = ui::componentHeader(name.c_str(), componentIcon(name), label.c_str(), [&]() {
      if (inherited) {
        if (ImGui::MenuItem(ICON_ARROW_U_UP_LEFT "  Revert to Prefab", nullptr, false,
                            entity.contains("overrides") && entity["overrides"].contains(name))) {
          scene->editEntities(targets, "Revert " + label, [&](Json& e) {
            if (e.contains("overrides")) e["overrides"].erase(name);
          });
        }
      } else {
        if (ImGui::MenuItem(ICON_ARROW_COUNTER_CLOCKWISE "  Reset") && schema) {
          Json fresh = Json::object();
          for (const FieldSchema& f : schema->fields) {
            if (!f.defaultValue.is_null()) fresh[f.key] = f.defaultValue;
          }
          scene->editEntities(targets, "Reset " + label, [&](Json& e) { editableComponent(e, name) = fresh; });
        }
      }
      if (ImGui::MenuItem(ICON_COPY "  Copy as JSON")) ImGui::SetClipboardText(component.dump(2).c_str());
      if (ImGui::MenuItem(ICON_CLIPBOARD_TEXT "  Paste JSON")) {
        const Json pasted = Json::parse(ImGui::GetClipboardText() ? ImGui::GetClipboardText() : "", nullptr, false);
        if (pasted.is_object()) {
          scene->editEntities(targets, "Paste " + label, [&](Json& e) { editableComponent(e, name) = pasted; });
        } else {
          editor.toasts().show(Toasts::Kind::Warning, "The clipboard has no JSON object");
        }
      }
      ImGui::Separator();
      ImGui::BeginDisabled(inherited);
      if (ImGui::MenuItem(ICON_TRASH "  Remove Component")) removeComponent = name;
      ImGui::EndDisabled();
      if (inherited) ui::tooltip("Comes from the prefab");
    });
    if (open) {
      ImGui::Indent(4);
      if (name == "ScriptComponent" && schema) {
        scriptSection(editor, ctx, component, entity, _jsonDrafts, _addFilter);
      } else if (name == "SpriteAnimationComponent" && schema) {
        animationSection(editor, ctx, component, entity, _jsonDrafts, _addFilter);
      } else if (name == "TileMapComponent" && schema) {
        tileMapSection(editor, ctx, component, entity, uid, _jsonDrafts, _addFilter);
      } else if (schema && ui::beginProperties("fields")) {
        for (const FieldSchema& f : schema->fields) {
          fieldRow(ctx, f, component, {}, inherited && overrides(entity, name, f.key), _jsonDrafts, _addFilter);
        }
        ui::endProperties();
      } else if (!schema && ui::beginProperties("raw")) {
        // No schema: each key edits as JSON.
        for (auto it = component.begin(); it != component.end(); ++it) {
          fieldRow(ctx, FieldSchema::json(it.key()), component, {}, inherited && overrides(entity, name, it.key()), _jsonDrafts,
                   _addFilter);
        }
        ui::endProperties();
      }
      if (name == "UIDocumentComponent" && component.value("src", std::string()) != "") {
        if (ui::button(ICON_CODE "  Edit Document", {-FLT_MIN, 0})) editor.openInCodeEditor(component["src"]);
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
  const float bw = std::min(ImGui::GetContentRegionAvail().x, 260.0f);
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - bw) * 0.5f);
  if (ui::button(ICON_PLUS "  Add Component", {bw, 32})) {
    _addFilter.clear();
    ImGui::OpenPopup("add component");
  }
  ImGui::SetNextWindowSize({300, 360});
  if (ImGui::BeginPopup("add component")) {
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ui::searchField("search", _addFilter, "Search components");
    ImGui::Dummy({0, 2});
    std::vector<std::pair<int, std::string>> matches;
    for (const auto& [name, schema] : componentSchemas()) {
      if (components.contains(name)) continue;
      const int score = _addFilter.empty() ? 0 : ui::fuzzyScore(componentLabel(name) + " " + schema.category, _addFilter);
      if (score >= 0) matches.emplace_back(score, name);
    }
    std::stable_sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) {
      return a.first != b.first ? a.first > b.first
                                : componentSchema(a.second)->category + componentLabel(a.second) <
                                      componentSchema(b.second)->category + componentLabel(b.second);
    });
    std::string category;
    bool first = true;
    for (const auto& [_, name] : matches) {
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
        Json initial = Json::object();
        for (const FieldSchema& f : schema->fields) {
          if (!f.defaultValue.is_null() && f.kind != Kind::Group) initial[f.key] = f.defaultValue;
        }
        scene->editEntities(targets, "Add " + componentLabel(name), [&](Json& e) { editableComponent(e, name) = initial; });
        ImGui::CloseCurrentPopup();
      }
    }
    if (matches.empty()) ui::dimText("Nothing matches.");
    ImGui::EndPopup();
  }
  ImGui::Dummy({0, 12});
}
