// The Scene view's shapes on the selected entity: its terrain's points and lines
// and a path follower's route (drag a point; double-click a line to add one, a point to remove it), the
// radius of its circle collider or else its point light, and a camera's view,
// dead zone, bounds and regions (drag a corner; double-click a region's to remove it).

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
  bool route = false;  // a PathFollowerComponent's points, not ground

  explicit Chain(const Json& c, bool route = false) : route(route) {
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

// A camera's rect with corner handles: its dead zone (around it), bounds or a region (in the world).
struct CameraBox {
  enum Kind { DeadZone, Bounds, Region } kind;
  int region = -1;
  glm::vec2 min{0.0f}, max{0.0f};
  // Corners counter-clockwise from the bottom-left.
  glm::vec2 corner(int i) const { return {i == 1 || i == 2 ? max.x : min.x, i >= 2 ? max.y : min.y}; }
};

std::optional<CameraBox> rectOf(const Json& r, CameraBox::Kind kind, int region = -1) {
  const auto a = r.is_object() ? pairOf(r.value("min", Json())) : std::nullopt;
  const auto b = r.is_object() ? pairOf(r.value("max", Json())) : std::nullopt;
  if (!a || !b) return std::nullopt;
  return CameraBox{kind, region, glm::min(*a, *b), glm::max(*a, *b)};
}

Json rectJson(glm::vec2 a, glm::vec2 b) {
  const glm::vec2 lo = glm::min(a, b), hi = glm::max(a, b);
  return {{"min", {lo.x, lo.y}}, {"max", {hi.x, hi.y}}};
}

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
  glm::vec2 view{0.0f};  // a camera's: what it shows, in world units (none: 0)
  std::vector<CameraBox> boxes;
  Json regionsJson = Json::array();
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
  if (c.contains("PathFollowerComponent") && c["PathFollowerComponent"].is_object()) {  // after the ground: its chains keep their indices
    const Json& follower = c["PathFollowerComponent"];
    s.chains.emplace_back(Json{{"points", follower.value("points", Json::array())}, {"closed", follower.value("mode", Json()) == "loop"}}, true);
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
  if (c.contains("CameraComponent") && c["CameraComponent"].is_object()) {
    const Json& camera = c["CameraComponent"];
    const Json zoom = camera.value("zoom", Json(1.0f));
    const float z = zoom.is_number() && zoom.get<float>() > 0.0f ? zoom.get<float>() : 1.0f;
    s.view = glm::vec2(editor.preview().gameSize()) / z;
    const glm::vec2 half = glm::max(pairOf(camera.value("deadZone", Json())).value_or(glm::vec2(0.0f)), glm::vec2(0.0f)) * 0.5f;
    if (half.x > 0.0f || half.y > 0.0f) s.boxes.push_back({CameraBox::DeadZone, -1, s.at - half, s.at + half});  // none: no handles on the gizmo
    if (auto bounds = rectOf(camera.value("bounds", Json()), CameraBox::Bounds)) s.boxes.push_back(*bounds);
    s.regionsJson = camera.value("regions", Json::array());
    if (!s.regionsJson.is_array()) s.regionsJson = Json::array();
    for (int i = 0; i < static_cast<int>(s.regionsJson.size()); ++i)
      if (auto region = rectOf(s.regionsJson[i], CameraBox::Region, i)) s.boxes.push_back(*region);
  }
  if (s.chains.empty() && !s.radius && s.view.x <= 0.0f && s.boxes.empty()) return std::nullopt;
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

// `verb`: "Move", "Add", "Remove" (the undo step's name).
void setChainPoints(Editor& editor, const Shapes& shapes, int chain, const std::vector<glm::vec2>& points,
                    const std::string& verb, const std::string& key = {}) {
  const std::string label = verb + (shapes.chains[chain].route ? " Path Point" : " Terrain Point");
  if (shapes.chains[chain].route) {
    editor.scene()->editEntity(shapes.uid, label, [&](Json& entity) { editableComponent(entity, "PathFollowerComponent")["points"] = pointsJson(points); }, key);
    return;
  }
  Json chains = shapes.chainsJson;
  chains[chain]["points"] = pointsJson(points);
  editor.scene()->editEntity(shapes.uid, label, [&](Json& entity) { editableComponent(entity, "GroundComponent")["chains"] = chains; }, key);
}

}  // namespace

std::optional<ScenePanel::ShapeHandle> ScenePanel::shapeHandleAt(Editor& editor, ImVec2 mouse) const {
  const auto s = shapesOf(editor);
  if (!s) return std::nullopt;
  if (s->radius && distance(mouse, toScreen(s->radiusHandle())) <= kReach) return RadiusHandle{};
  for (int b = 0; b < static_cast<int>(s->boxes.size()); ++b)
    for (int i = 0; i < 4; ++i)
      if (distance(mouse, toScreen(s->boxes[b].corner(i))) <= kReach) return CornerHandle{b, i};
  for (int c = 0; c < static_cast<int>(s->chains.size()); ++c) {
    if (!s->chains[c].wellFormed) continue;
    for (int i = 0; i < static_cast<int>(s->chains[c].points.size()); ++i) {
      const glm::vec2 p = s->chains[c].points[i];
      if (s->chains[c].route && p == glm::vec2(0.0f)) continue;  // a route's start is the entity itself: move it
      if (distance(mouse, toScreen(s->at + p)) <= kReach) return PointHandle{c, i};
    }
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
    const ImU32 color = chain.route ? theme::u32(theme::violet) : theme::u32(theme::accent);
    for (int i = 0; i < chain.segments(); ++i)
      draw->AddLine(toScreen(s->at + chain.end(i, 0)), toScreen(s->at + chain.end(i, 1)), chain.route ? theme::u32(theme::violet, 0.7f) : line, 1.5f);
    for (int i = 0; i < (chain.wellFormed ? static_cast<int>(chain.points.size()) : 0); ++i) {
      const ImVec2 p = toScreen(s->at + chain.points[i]);
      const auto* held = hot ? std::get_if<PointHandle>(&*hot) : nullptr;
      const bool lit = held && held->chain == c && held->point == i;
      draw->AddRectFilled({p.x - 4, p.y - 4}, {p.x + 4, p.y + 4}, theme::u32(lit ? theme::warning : theme::bg0));
      draw->AddRect({p.x - 4, p.y - 4}, {p.x + 4, p.y + 4}, color, 0.0f, 1.5f);
    }
  }
  if (s->view.x > 0.0f) {
    const glm::vec2 half = s->view * 0.5f;
    draw->AddRect(toScreen(s->at + glm::vec2(-half.x, half.y)), toScreen(s->at + glm::vec2(half.x, -half.y)), theme::u32(theme::text, 0.6f), 0.0f, 1.5f);
  }
  for (int b = 0; b < static_cast<int>(s->boxes.size()); ++b) {
    const CameraBox& box = s->boxes[b];
    const ImVec4 color = box.kind == CameraBox::DeadZone ? theme::accent : box.kind == CameraBox::Bounds ? theme::warning : theme::violet;
    draw->AddRect(toScreen(box.corner(3)), toScreen(box.corner(1)), theme::u32(color, 0.7f), 0.0f, 1.5f);
    for (int i = 0; i < 4; ++i) {
      const ImVec2 p = toScreen(box.corner(i));
      const auto* held = hot ? std::get_if<CornerHandle>(&*hot) : nullptr;
      const bool lit = held && held->box == b && held->corner == i;
      draw->AddRectFilled({p.x - 4, p.y - 4}, {p.x + 4, p.y + 4}, theme::u32(lit ? theme::warning : theme::bg0));
      draw->AddRect({p.x - 4, p.y - 4}, {p.x + 4, p.y + 4}, theme::u32(color), 0.0f, 1.5f);
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
    const auto* corner = std::get_if<CornerHandle>(&*_shape);
    const int region = corner ? s->boxes[corner->box].region : -1;
    _shape.reset();
    if (region >= 0) {  // a region's corner: the region goes
      Json regions = s->regionsJson;
      regions.erase(static_cast<size_t>(region));
      editor.scene()->editEntity(s->uid, "Remove Camera Region", [&](Json& e) {
        editableComponent(e, "CameraComponent")["regions"] = regions;  // whole: a prefab's list is the override's start
      });
      return true;
    }
    if (!held) return true;
    std::vector<glm::vec2> points = s->chains[held->chain].points;
    if (points.size() <= s->chains[held->chain].fewest()) return true;  // a line needs two ends, a closed shape three
    points.erase(points.begin() + held->point);
    setChainPoints(editor, *s, held->chain, points, "Remove");
    return true;
  }
  if (!twice) return false;
  for (int c = 0; c < static_cast<int>(s->chains.size()); ++c) {
    const Chain& chain = s->chains[c];
    for (int i = 0; i < chain.segments(); ++i) {
      if (distanceToSegment(mouse, toScreen(s->at + chain.end(i, 0)), toScreen(s->at + chain.end(i, 1))) > kReach) continue;
      std::vector<glm::vec2> points = chain.points;
      points.insert(points.begin() + i + 1, glm::round(snapped(toWorld(mouse), false)) - s->at);  // on the closing edge: last
      setChainPoints(editor, *s, c, points, "Add");
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
  if (const auto* corner = std::get_if<CornerHandle>(&*_shape)) {
    if (corner->box >= static_cast<int>(s->boxes.size())) return;
    const CameraBox& box = s->boxes[corner->box];
    const glm::vec2 to = glm::round(snapped(world, false));
    editor.scene()->editEntity(s->uid, "Change Camera", [&](Json& e) {
      Json& camera = editableComponent(e, "CameraComponent");
      if (box.kind == CameraBox::DeadZone) {
        const glm::vec2 size = glm::abs(to - s->at) * 2.0f;
        camera["deadZone"] = {size.x, size.y};
        return;
      }
      const Json rect = rectJson(to, box.corner((corner->corner + 2) % 4));  // the opposite corner stays
      if (box.kind == CameraBox::Bounds) {
        camera["bounds"] = rect;
        return;
      }
      Json regions = s->regionsJson;
      regions[static_cast<size_t>(box.region)] = rect;
      camera["regions"] = regions;
    }, key);
    // Dragged past the opposite corner: it's another corner now.
    const glm::vec2 opposite = box.corner((corner->corner + 2) % 4);
    const bool right = to.x > opposite.x, top = to.y > opposite.y;
    std::get<CornerHandle>(*_shape).corner = top ? (right ? 2 : 3) : (right ? 1 : 0);
    return;
  }
  const PointHandle held = std::get<PointHandle>(*_shape);
  if (held.chain >= static_cast<int>(s->chains.size()) || held.point >= static_cast<int>(s->chains[held.chain].points.size())) return;
  std::vector<glm::vec2> points = s->chains[held.chain].points;
  points[held.point] = glm::round(snapped(world, false)) - s->at;
  setChainPoints(editor, *s, held.chain, points, "Move", key);
}
