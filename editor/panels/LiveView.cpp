// The running game, live: its entities in the Hierarchy, the selected one's
// transform and script-visible fields in the Inspector (tweakable until
// Stop), and an outline in the Game view.

#include <algorithm>
#include <bit>
#include <cmath>

#include <glm/gtc/constants.hpp>

#include "Entities.hpp"
#include "Icons.hpp"
#include "Panels.hpp"
#include "Theme.hpp"
#include "Ui.hpp"
#include "physics2d/TransformComponent.hpp"
#include "renderer2d/Renderer2D.hpp"

namespace {

// What to call a running entity: its first tag, else what it is ("Sprite 12").
std::string liveName(Editor& editor, World& world, EntityId id) {
  const auto tags = world.tagNames(id);
  // Scene entities carry their name as a tag: that's the name to show, whatever else scripts tag them with.
  if (SceneDocument* scene = editor.scene()) {
    for (size_t i = 0; i < scene->size(); ++i) {
      if (std::find(tags.begin(), tags.end(), scene->displayName(i)) != tags.end()) return scene->displayName(i);
    }
  }
  if (!tags.empty()) return tags.front();
  const auto components = world.componentNames(id);
  for (const char* telling : {"TileMapComponent", "UIDocumentComponent", "TextComponent", "SpriteAnimationComponent",
                              "SpriteComponent", "AudioEmitterComponent", "BoxColliderComponent", "ScriptComponent"}) {
    if (std::find(components.begin(), components.end(), telling) != components.end()) {
      return componentLabel(telling) + " " + std::to_string(id.index);
    }
  }
  return "Entity " + std::to_string(id.index);
}

const char* liveIcon(const std::vector<std::string>& components) {
  Json keys = Json::object();
  for (const auto& c : components) keys[c] = true;
  return entityIcon(keys);
}

// Four script fields that edit as one color: r/g/b/a, or xR/xG/xB/xA (or xAlpha). Returns its label, empty if none.
std::string colorAt(const std::vector<ScriptField>& fields, size_t at) {
  if (at + 3 >= fields.size()) return {};
  const std::string& first = fields[at].name;
  const bool lower = first == "r";
  if (!lower && !first.ends_with("R")) return {};
  const std::string stem = first.substr(0, first.size() - 1);
  const std::string& alpha = fields[at + 3].name;
  const bool color = fields[at + 1].name == stem + (lower ? "g" : "G") && fields[at + 2].name == stem + (lower ? "b" : "B") &&
                     (alpha == stem + (lower ? "a" : "A") || alpha == stem + "Alpha");
  return !color ? std::string() : lower ? "color" : stem;
}

}  // namespace

void HierarchyPanel::drawLive(Editor& editor) {
  World& world = editor.game()->engine().getWorld();
  std::vector<EntityId> ids = world.entities();
  std::sort(ids.begin(), ids.end(), [](EntityId a, EntityId b) { return a.index < b.index; });
  if (editor.liveSelection() && !world.isAlive(*editor.liveSelection())) editor.selectLive(std::nullopt);

  std::vector<std::pair<EntityId, std::string>> rows;
  for (EntityId id : ids) {
    std::string name = liveName(editor, world, id);
    if (!_filter.empty() && ui::fuzzyScore(name, _filter) < 0) continue;
    rows.emplace_back(id, std::move(name));
  }
  ImGui::BeginChild("##live", {0, -ImGui::GetTextLineHeightWithSpacing() - 6});
  ImDrawList* draw = ImGui::GetWindowDrawList();
  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(rows.size()), 26.0f);
  while (clipper.Step()) {
    for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
      const auto& [id, name] = rows[static_cast<size_t>(r)];
      ImGui::PushID(static_cast<int>(id.index));
      const ImVec2 pos = ImGui::GetCursorScreenPos();
      const float width = ImGui::GetContentRegionAvail().x;
      if (ImGui::InvisibleButton("##row", {width, 26.0f})) editor.selectLive(id);
      const bool selected = editor.liveSelection() == id;
      if (selected || ImGui::IsItemHovered()) {
        draw->AddRectFilled(pos, {pos.x + width, pos.y + 26}, selected ? theme::u32(theme::selection) : theme::u32(theme::text, 0.05f),
                            theme::radius);
      }
      const float ty = pos.y + (26 - ImGui::GetTextLineHeight()) * 0.5f;
      draw->AddText({pos.x + 8, ty}, theme::u32(selected ? theme::accentBright : theme::textDim), liveIcon(world.componentNames(id)));
      draw->AddText({pos.x + 30, ty}, theme::u32(theme::text), name.c_str());
      ImGui::PushFont(nullptr, theme::sizeSmall);
      const std::string number = "#" + std::to_string(id.index);
      const ImVec2 ns = ImGui::CalcTextSize(number.c_str());
      draw->AddText({pos.x + width - ns.x - 8, ty + 1}, theme::u32(theme::textFaint), number.c_str());
      ImGui::PopFont();
      ImGui::PopID();
    }
  }
  if (rows.empty()) ui::dimText(ids.empty() ? "  Nothing is running." : "  No entities match.");
  ImGui::EndChild();
  ImGui::PushFont(nullptr, theme::sizeSmall);
  ImGui::TextColored(theme::accent, ICON_PLAY_CIRCLE);
  ImGui::SameLine(0, 4);
  ImGui::TextColored(theme::textFaint, "%zu running %s", ids.size(), ids.size() == 1 ? "entity" : "entities");
  ImGui::PopFont();
}

void InspectorPanel::drawLive(Editor& editor, EntityId id) {
  World& world = editor.game()->engine().getWorld();
  const auto components = world.componentNames(id);
  const auto tags = world.tagNames(id);

  ImGui::PushFont(nullptr, 18.0f);
  ImGui::TextColored(theme::accent, "%s", liveIcon(components));
  ImGui::PopFont();
  ImGui::SameLine(0, 8);
  ImGui::BeginGroup();
  ui::heading(liveName(editor, world, id).c_str());
  ui::smallText("Running game: changes last until Stop", theme::accent);
  ImGui::EndGroup();
  if (tags.size() > 1) {
    std::string list;
    for (const auto& t : tags) list += (list.empty() ? "#" : "  #") + t;
    ui::smallText(list.c_str(), theme::textDim);
  }
  ImGui::Dummy({0, 4});

  if (auto* t = world.getComponent<TransformComponent>(id)) {
    if (ui::componentHeader("live transform", componentIcon("TransformComponent"), "Transform", {})) {
      if (ui::beginProperties("lt")) {
        ui::propertyRow("Position");
        float p[3] = {t->position.x, t->position.y, t->position.z};
        if (ui::dragVector("##p", p, 3, 0.5f, "%.4g")) t->position = {p[0], p[1], p[2]};
        ui::propertyRow("Scale");
        float s[2] = {t->scale.x, t->scale.y};
        if (ui::dragVector("##s", s, 2, 0.1f, "%.4g")) t->scale = {s[0], s[1]};
        ui::propertyRow("Rotation");
        float degrees = t->rotationRad * 180.0f / glm::pi<float>();
        if (ImGui::DragFloat("##r", &degrees, 0.5f, 0, 0, "%.1f\xC2\xB0")) t->rotationRad = degrees * glm::pi<float>() / 180.0f;
        ui::endProperties();
      }
    }
  }
  const ComponentRegistry& registry = world.getComponentRegistry();
  for (const std::string& name : components) {
    if (name == "TransformComponent") continue;
    auto componentId = registry.getComponentIdByName(name);
    const ComponentInfo* info = componentId ? registry.getInfo(*componentId) : nullptr;
    if (!info) continue;
    ImGui::PushID(name.c_str());
    if (ui::componentHeader(name.c_str(), componentIcon(name), componentLabel(name).c_str(), {})) {
      if (info->scriptFields.empty()) {
        ui::smallText("Nothing here changes live.", theme::textFaint);
      } else if (ui::beginProperties("fields")) {
        for (uint32_t i = 0; i < info->scriptFields.size(); ++i) {
          const ScriptField& field = info->scriptFields[i];
          const World::ScriptFieldRef ref{info, i};
          if (const std::string label = colorAt(info->scriptFields, i); !label.empty()) {
            float c[4];
            bool readable = true;
            for (uint32_t k = 0; k < 4; ++k) {
              auto b = world.readScriptField(id, {info, i + k});
              readable &= b.has_value();
              c[k] = b ? std::bit_cast<float>(*b) : 0.0f;
            }
            if (readable) {
              ui::propertyRow(label.c_str());
              ImGui::PushID(static_cast<int>(i));
              if (ImGui::ColorEdit4("##c", c, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_Float)) {
                for (uint32_t k = 0; k < 4; ++k) world.writeScriptField(id, {info, i + k}, std::bit_cast<uint32_t>(c[k]));
              }
              ImGui::PopID();
              i += 3;
              continue;
            }
          }
          auto bits = world.readScriptField(id, ref);
          if (!bits) continue;
          ui::propertyRow(field.name.c_str());
          ImGui::PushID(static_cast<int>(i));
          if (field.integer) {
            int value = static_cast<int>(*bits);
            if (ImGui::DragInt("##v", &value, 0.1f)) world.writeScriptField(id, ref, static_cast<uint32_t>(value));
          } else {
            float value = std::bit_cast<float>(*bits);
            if (!std::isfinite(value)) {
              // Unset (NaN means "the engine decides"): click to give it a value.
              ImGui::PushStyleColor(ImGuiCol_Text, theme::textDim);
              if (ImGui::Button("Auto##v", {-1, 0})) world.writeScriptField(id, ref, std::bit_cast<uint32_t>(0.0f));
              ImGui::PopStyleColor();
              if (ImGui::IsItemHovered()) ImGui::SetTooltip("Left for the engine to decide. Click to set it.");
            } else if (ImGui::DragFloat("##v", &value, std::max(0.01f, std::abs(value) * 0.01f), 0, 0, "%.4g")) {
              world.writeScriptField(id, ref, std::bit_cast<uint32_t>(value));
            }
          }
          ImGui::PopID();
        }
        ui::endProperties();
      }
    }
    ImGui::PopID();
  }
}

std::optional<std::array<ImVec2, 4>> GamePanel::liveOutline(Editor& editor, ImVec2 at, ImVec2 size) {
  auto selected = editor.liveSelection();
  HostedEngine* game = editor.game();
  if (!selected || !game) return std::nullopt;
  World& world = game->engine().getWorld();
  auto* t = world.isAlive(*selected) ? world.getComponent<TransformComponent>(*selected) : nullptr;
  if (!t) return std::nullopt;
  Renderer2D& r = game->renderer().renderer();
  const glm::vec2 logical(r.logicalSize());
  const glm::vec4 vp = r.gameViewport();
  const glm::vec2 frame(r.frameSize());
  const glm::vec2 camera = r.camera().position();
  const float zoom = r.camera().zoom();
  auto toScreen = [&](glm::vec2 w) {
    const glm::vec2 l{logical.x * 0.5f + (w.x - camera.x) * zoom, logical.y * 0.5f - (w.y - camera.y) * zoom};
    const glm::vec2 fb{vp.x + l.x * vp.z / logical.x, (frame.y - vp.y - vp.w) + l.y * vp.w / logical.y};
    return ImVec2(at.x + fb.x * size.x / frame.x, at.y + fb.y * size.y / frame.y);
  };
  const glm::vec2 half = glm::max(glm::abs(t->scale), glm::vec2(4.0f));
  const glm::vec2 c(t->position);
  return std::array<ImVec2, 4>{toScreen(c + glm::vec2(-half.x, -half.y)), toScreen(c + glm::vec2(half.x, -half.y)),
                               toScreen(c + glm::vec2(half.x, half.y)), toScreen(c + glm::vec2(-half.x, half.y))};
}
