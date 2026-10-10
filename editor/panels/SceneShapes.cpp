// The Scene view's shape handles on the selected entity: its terrain's points
// (drag one; double-click a line to add one, a point to remove it) and its circle's radius.

#include <cmath>

#include "Entities.hpp"
#include "Panels.hpp"
#include "Theme.hpp"

namespace {

constexpr float kReach = 7.0f;  // how near the mouse (points) takes a handle or a line

// The selected entity's shapes, as the engine will see them, and where it is.
struct Shapes {
  EntityUid uid = 0;
  glm::vec2 at{0.0f};
  Json chains = Json::array();
  std::optional<float> radius;
  glm::vec2 circle{0.0f};  // the circle's center, in the world
};

std::optional<Shapes> shapesOf(Editor& editor) {
  if (editor.selection().size() != 1) return std::nullopt;
  const Json* e = editor.scene()->find(editor.primary());
  const auto t = editor.worldTransform(editor.primary());
  if (!e || !t) return std::nullopt;
  const Json c = effectiveComponents(*editor.project(), *e);
  Shapes s{editor.primary(), glm::vec2(t->position)};
  if (c.contains("TerrainComponent")) s.chains = c["TerrainComponent"].value("chains", Json::array());
  if (c.contains("CircleColliderComponent")) {
    const Json& circle = c["CircleColliderComponent"];
    s.radius = circle.value("radius", 8.0f);
    const Json offset = circle.value("offset", Json::array({0, 0}));
    s.circle = s.at + glm::vec2(offset[0].get<float>(), offset[1].get<float>());
  }
  if (!s.chains.is_array() || (s.chains.empty() && !s.radius)) return std::nullopt;
  return s;
}

glm::vec2 pointOf(const Json& p) { return {p[0].get<float>(), p[1].get<float>()}; }

float distanceToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
  const ImVec2 ab{b.x - a.x, b.y - a.y}, ap{p.x - a.x, p.y - a.y};
  const float len2 = ab.x * ab.x + ab.y * ab.y;
  const float t = len2 > 0.0f ? std::clamp((ap.x * ab.x + ap.y * ab.y) / len2, 0.0f, 1.0f) : 0.0f;
  return std::hypot(p.x - (a.x + ab.x * t), p.y - (a.y + ab.y * t));
}

}  // namespace

void ScenePanel::drawShapeHandles(Editor& editor, ImDrawList* draw) {
  const auto s = shapesOf(editor);
  if (!s) return;
  const ImVec2 mouse = ImGui::GetMousePos();
  for (int c = 0; c < static_cast<int>(s->chains.size()); ++c) {
    const Json& points = s->chains[c].value("points", Json::array());
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
      const ImVec2 p = toScreen(s->at + pointOf(points[i]));
      const bool hot = (_drag == Drag::Shape && _shape && !_shape->radius && _shape->chain == c && _shape->point == i) ||
                       (_drag == Drag::None && std::hypot(mouse.x - p.x, mouse.y - p.y) <= kReach);
      draw->AddRectFilled({p.x - 4, p.y - 4}, {p.x + 4, p.y + 4}, theme::u32(hot ? theme::warning : theme::bg0));
      draw->AddRect({p.x - 4, p.y - 4}, {p.x + 4, p.y + 4}, theme::u32(theme::accent), 0.0f, 1.5f);
    }
  }
  if (s->radius) {
    const ImVec2 p = toScreen(s->circle + glm::vec2(*s->radius, 0.0f));
    draw->AddCircleFilled(p, 5.0f, theme::u32(_drag == Drag::Shape && _shape && _shape->radius ? theme::warning : theme::accent));
  }
}

bool ScenePanel::startShapeDrag(Editor& editor, ImVec2 mouse) {
  const auto s = shapesOf(editor);
  if (!s) return false;
  const bool twice = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
  if (s->radius) {
    const ImVec2 p = toScreen(s->circle + glm::vec2(*s->radius, 0.0f));
    if (std::hypot(mouse.x - p.x, mouse.y - p.y) <= kReach) {
      _shape = ShapeHandle{true};
      return true;
    }
  }
  for (int c = 0; c < static_cast<int>(s->chains.size()); ++c) {
    Json points = s->chains[c].value("points", Json::array());
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
      const ImVec2 p = toScreen(s->at + pointOf(points[i]));
      if (std::hypot(mouse.x - p.x, mouse.y - p.y) > kReach) continue;
      if (!twice) {
        _shape = ShapeHandle{false, c, i};
        return true;
      }
      if (points.size() <= 2) return true;  // a line needs two ends
      points.erase(static_cast<size_t>(i));
      setChainPoints(editor, s->uid, c, std::move(points), "Remove Terrain Point");
      _shape.reset();
      return true;
    }
  }
  for (int c = 0; c < static_cast<int>(s->chains.size()); ++c) {
    Json points = s->chains[c].value("points", Json::array());
    for (int i = 0; i + 1 < static_cast<int>(points.size()); ++i) {
      if (distanceToSegment(mouse, toScreen(s->at + pointOf(points[i])), toScreen(s->at + pointOf(points[i + 1]))) > kReach) continue;
      _shape.reset();
      if (!twice) return true;  // on its line: still it, selected
      const glm::vec2 local = glm::round(snapped(toWorld(mouse), false)) - s->at;
      points.insert(points.begin() + i + 1, Json::array({local.x, local.y}));
      setChainPoints(editor, s->uid, c, std::move(points), "Add Terrain Point");
      return true;
    }
  }
  return false;
}

void ScenePanel::applyShapeDrag(Editor& editor, glm::vec2 world) {
  const auto s = shapesOf(editor);
  if (!s || !_shape) return;
  const std::string key = gestureKey("scene-shape", false);
  if (_shape->radius) {
    if (!s->radius) return;
    const float radius = std::round(std::max(glm::length(world - s->circle), 0.5f) * 2.0f) / 2.0f;  // half units
    editor.scene()->editEntity(s->uid, "Resize Circle", [&](Json& e) {
      editableComponent(e, "CircleColliderComponent")["radius"] = radius;
    }, key);
    return;
  }
  if (_shape->chain >= static_cast<int>(s->chains.size())) return;
  Json points = s->chains[_shape->chain].value("points", Json::array());
  if (_shape->point >= static_cast<int>(points.size())) return;
  const glm::vec2 local = glm::round(snapped(world, false)) - s->at;
  points[_shape->point] = Json::array({local.x, local.y});
  setChainPoints(editor, s->uid, _shape->chain, std::move(points), "Move Terrain Point", key);
}

void ScenePanel::setChainPoints(Editor& editor, EntityUid uid, int chain, Json points, const std::string& label,
                                const std::string& key) {
  const Json* e = editor.scene()->find(uid);
  if (!e) return;
  Json chains = effectiveComponents(*editor.project(), *e)["TerrainComponent"].value("chains", Json::array());
  if (chain >= static_cast<int>(chains.size())) return;
  chains[chain]["points"] = std::move(points);
  editor.scene()->editEntity(uid, label, [&](Json& entity) { editableComponent(entity, "TerrainComponent")["chains"] = chains; }, key);
}
