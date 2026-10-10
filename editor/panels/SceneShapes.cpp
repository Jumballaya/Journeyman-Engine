// The Scene view's shapes on the selected entity: its terrain's points and lines
// (drag a point; double-click a line to add one, a point to remove it) and the
// radius of its circle collider, or else of its point light.

#include <cmath>
#include <string_view>
#include <utility>

#include "Entities.hpp"
#include "Panels.hpp"
#include "Theme.hpp"

namespace {

constexpr float kReach = 7.0f;  // how near the mouse (points) takes a handle or a line

std::optional<glm::vec2> pairOf(const Json& p) {
  if (!p.is_array() || p.size() != 2 || !p[0].is_number() || !p[1].is_number()) return std::nullopt;
  const glm::vec2 v(p[0].get<float>(), p[1].get<float>());
  return std::isfinite(v.x) && std::isfinite(v.y) ? std::optional(v) : std::nullopt;
}

// A chain as the engine reads it; a malformed one (the engine skips it) isn't shown or edited.
struct Chain {
  std::vector<glm::vec2> points;  // from the entity
  bool closed = false;
  bool wellFormed = false;

  explicit Chain(const Json& c) {
    const Json points = c.is_object() ? c.value("points", Json()) : Json();
    if (!points.is_array()) return;
    for (const Json& p : points) {
      const auto v = pairOf(p);
      if (!v) return;
      this->points.push_back(*v);
    }
    closed = c.value("closed", Json(false)).is_boolean() && c.value("closed", false);
    wellFormed = true;
  }
  int segments() const {
    const int n = static_cast<int>(points.size());
    return !wellFormed || n < 2 ? 0 : closed && n > 2 ? n : n - 1;
  }
  glm::vec2 end(int segment, int which) const { return points[(segment + which) % points.size()]; }
  size_t fewest() const { return closed ? 3 : 2; }
};

// The selected entity's shapes, as the engine will see them, and where it is.
struct Shapes {
  EntityUid uid = 0;
  glm::vec2 at{0.0f};
  Json chainsJson = Json::array();  // as written, to edit
  std::vector<Chain> chains;
  std::optional<float> radius;
  const char* radiusOf = "";  // the component it's from
  glm::vec2 circle{0.0f};     // its center, in the world
  glm::vec2 radiusHandle() const { return circle + *radius * glm::vec2(0.7071f); }  // at 45°: off the gizmo's arrows
};

std::optional<Shapes> shapesOf(Editor& editor) {
  if (editor.selection().size() != 1) return std::nullopt;
  const Json* e = editor.scene()->find(editor.primary());
  const auto t = editor.worldTransform(editor.primary());
  if (!e || !t) return std::nullopt;
  const Json c = effectiveComponents(*editor.project(), *e);
  Shapes s{editor.primary(), glm::vec2(t->position)};
  if (c.contains("GroundComponent") && c["GroundComponent"].is_object()) {
    s.chainsJson = c["GroundComponent"].value("chains", Json::array());
    if (!s.chainsJson.is_array()) s.chainsJson = Json::array({s.chainsJson});  // one chain, as the engine reads it
    for (const Json& chain : s.chainsJson) s.chains.emplace_back(chain);
  }
  // A collider's radius first: it's what the game plays by (the engine's defaults when unset).
  for (const auto& [name, fallback] : {std::pair{"CircleColliderComponent", 8.0f}, std::pair{"PointLightComponent", 128.0f}}) {
    if (s.radius || !c.contains(name) || !c[name].is_object()) continue;
    const Json& circle = c[name];
    const Json radius = circle.value("radius", Json(fallback));
    if (radius.is_number() && std::isfinite(radius.get<float>())) {
      s.radius = std::max(radius.get<float>(), 0.0f);
      s.radiusOf = name;
      s.circle = s.at + pairOf(circle.value("offset", Json())).value_or(glm::vec2(0.0f));
    }
  }
  if (s.chains.empty() && !s.radius) return std::nullopt;
  return s;
}

float distanceToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
  const ImVec2 ab{b.x - a.x, b.y - a.y}, ap{p.x - a.x, p.y - a.y};
  const float len2 = ab.x * ab.x + ab.y * ab.y;
  const float t = len2 > 0.0f ? std::clamp((ap.x * ab.x + ap.y * ab.y) / len2, 0.0f, 1.0f) : 0.0f;
  return std::hypot(p.x - (a.x + ab.x * t), p.y - (a.y + ab.y * t));
}

float distance(ImVec2 a, ImVec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

Json pointsJson(const std::vector<glm::vec2>& points) {
  Json out = Json::array();
  for (glm::vec2 p : points) out.push_back(Json::array({p.x, p.y}));
  return out;
}

void setChainPoints(Editor& editor, const Shapes& shapes, int chain, const std::vector<glm::vec2>& points,
                    const std::string& label, const std::string& key = {}) {
  Json chains = shapes.chainsJson;
  chains[chain]["points"] = pointsJson(points);
  editor.scene()->editEntity(shapes.uid, label, [&](Json& entity) { editableComponent(entity, "GroundComponent")["chains"] = chains; }, key);
}

}  // namespace

std::optional<ScenePanel::ShapeHandle> ScenePanel::shapeHandleAt(Editor& editor, ImVec2 mouse) const {
  const auto s = shapesOf(editor);
  if (!s) return std::nullopt;
  if (s->radius && distance(mouse, toScreen(s->radiusHandle())) <= kReach) return RadiusHandle{};
  for (int c = 0; c < static_cast<int>(s->chains.size()); ++c) {
    if (!s->chains[c].wellFormed) continue;
    for (int i = 0; i < static_cast<int>(s->chains[c].points.size()); ++i)
      if (distance(mouse, toScreen(s->at + s->chains[c].points[i])) <= kReach) return PointHandle{c, i};
  }
  return std::nullopt;
}

bool ScenePanel::onShapeLine(Editor& editor, ImVec2 mouse) const {
  const auto s = shapesOf(editor);
  if (!s) return false;
  for (const Chain& chain : s->chains)
    for (int i = 0; i < chain.segments(); ++i)
      if (distanceToSegment(mouse, toScreen(s->at + chain.end(i, 0)), toScreen(s->at + chain.end(i, 1))) <= kReach) return true;
  return false;
}

void ScenePanel::drawShapeHandles(Editor& editor, ImDrawList* draw) {
  const auto s = shapesOf(editor);
  if (!s) return;
  const auto hot = _drag == Drag::Shape ? _shape : _drag == Drag::None ? shapeHandleAt(editor, ImGui::GetMousePos()) : std::nullopt;
  const ImU32 line = theme::u32(theme::accent, 0.7f);
  for (int c = 0; c < static_cast<int>(s->chains.size()); ++c) {
    const Chain& chain = s->chains[c];
    for (int i = 0; i < chain.segments(); ++i)
      draw->AddLine(toScreen(s->at + chain.end(i, 0)), toScreen(s->at + chain.end(i, 1)), line, 1.5f);
    for (int i = 0; i < (chain.wellFormed ? static_cast<int>(chain.points.size()) : 0); ++i) {
      const ImVec2 p = toScreen(s->at + chain.points[i]);
      const auto* held = hot ? std::get_if<PointHandle>(&*hot) : nullptr;
      const bool lit = held && held->chain == c && held->point == i;
      draw->AddRectFilled({p.x - 4, p.y - 4}, {p.x + 4, p.y + 4}, theme::u32(lit ? theme::warning : theme::bg0));
      draw->AddRect({p.x - 4, p.y - 4}, {p.x + 4, p.y + 4}, theme::u32(theme::accent), 0.0f, 1.5f);
    }
  }
  if (s->radius) {
    const bool light = std::string_view(s->radiusOf) == "PointLightComponent";
    draw->AddCircle(toScreen(s->circle), *s->radius * _zoom, light ? theme::u32(theme::warning, 0.7f) : line, 0, 1.5f);
    const bool lit = hot && std::holds_alternative<RadiusHandle>(*hot);
    draw->AddCircleFilled(toScreen(s->radiusHandle()), 5.0f, theme::u32(lit ? theme::warning : theme::accent));
  }
}

bool ScenePanel::pressShapes(Editor& editor, ImVec2 mouse) {
  const auto s = shapesOf(editor);
  if (!s) return false;
  const bool twice = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
  _shape = shapeHandleAt(editor, mouse);
  if (_shape && !twice) return true;  // held: dragging it
  if (_shape) {
    const std::optional<PointHandle> held =
        std::holds_alternative<PointHandle>(*_shape) ? std::optional(std::get<PointHandle>(*_shape)) : std::nullopt;
    _shape.reset();
    if (!held) return true;
    std::vector<glm::vec2> points = s->chains[held->chain].points;
    if (points.size() <= s->chains[held->chain].fewest()) return true;  // a line needs two ends, a closed shape three
    points.erase(points.begin() + held->point);
    setChainPoints(editor, *s, held->chain, points, "Remove Terrain Point");
    return true;
  }
  if (!twice) return false;
  for (int c = 0; c < static_cast<int>(s->chains.size()); ++c) {
    const Chain& chain = s->chains[c];
    for (int i = 0; i < chain.segments(); ++i) {
      if (distanceToSegment(mouse, toScreen(s->at + chain.end(i, 0)), toScreen(s->at + chain.end(i, 1))) > kReach) continue;
      std::vector<glm::vec2> points = chain.points;
      points.insert(points.begin() + i + 1, glm::round(snapped(toWorld(mouse), false)) - s->at);  // on the closing edge: last
      setChainPoints(editor, *s, c, points, "Add Terrain Point");
      return true;
    }
  }
  return false;
}

void ScenePanel::applyShapeDrag(Editor& editor, glm::vec2 world) {
  const auto s = shapesOf(editor);
  if (!s || !_shape) return;
  const std::string key = gestureKey("scene-shape", false);
  if (std::holds_alternative<RadiusHandle>(*_shape)) {
    if (!s->radius) return;
    const float radius = std::round(std::max(glm::length(world - s->circle), 0.5f) * 2.0f) / 2.0f;  // half units
    const bool light = std::string_view(s->radiusOf) == "PointLightComponent";
    editor.scene()->editEntity(s->uid, light ? "Resize Light" : "Resize Circle", [&](Json& e) {
      editableComponent(e, s->radiusOf)["radius"] = radius;
    }, key);
    return;
  }
  const PointHandle held = std::get<PointHandle>(*_shape);
  if (held.chain >= static_cast<int>(s->chains.size()) || held.point >= static_cast<int>(s->chains[held.chain].points.size())) return;
  std::vector<glm::vec2> points = s->chains[held.chain].points;
  points[held.point] = glm::round(snapped(world, false)) - s->at;
  setChainPoints(editor, *s, held.chain, points, "Move Terrain Point", key);
}
