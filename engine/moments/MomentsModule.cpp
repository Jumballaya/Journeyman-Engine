#include "MomentsModule.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <tuple>

#include "../core/app/Engine.hpp"
#include "../core/app/ModuleTags.hpp"
#include "../core/app/ModuleTraits.hpp"
#include "../core/app/Registration.hpp"
#include "../core/app/agent/AgentRun.hpp"
#include "../physics2d/BoxColliderComponent.hpp"
#include "../physics2d/CircleColliderComponent.hpp"
#include "../physics2d/TransformComponent.hpp"
#include "../renderer2d/Letterbox.hpp"
#include "../renderer2d/Renderer2DModule.hpp"
#include "../renderer2d/SpriteComponent.hpp"
#include "../tilemap/TileMapComponent.hpp"
#include "../ui/UIModule.hpp"

#include <GLFW/glfw3.h>  // after the renderer's GL loader

template <>
struct ModuleTraits<MomentsModule> {
  using Provides = TypeList<>;
  using DependsOn = TypeList<Renderer2DTag, UITag>;  // drawn over the UI
};

REGISTER_MODULE(MomentsModule)

namespace {

constexpr uint32_t kPicked = 0xFFD23F;  // outlines and names: readable on most games
constexpr const char* kBadge = "Paused · F8 · Enter saves";
constexpr float kNear = 6.0f;   // logical px: a click this near a point or an area's edge is on it
constexpr float kDrag = 4.0f;   // a press moved further is a drag
constexpr size_t kListed = 24;  // entities listed with a point or an area; then a count
constexpr size_t kCells = 64;   // tile cells listed per layer; then a count

nlohmann::json idJson(EntityId id) { return {id.index, id.generation}; }

nlohmann::json failure(std::string message) { return {{"ok", false}, {"error", std::move(message)}}; }

// Whether a quad (turned, convex) and a box overlap: no axis of either's edges separates them.
bool touches(const std::array<glm::vec2, 4>& quad, glm::vec2 lo, glm::vec2 hi) {
  const std::array<glm::vec2, 4> box{lo, glm::vec2(hi.x, lo.y), hi, glm::vec2(lo.x, hi.y)};
  for (const glm::vec2 axis : {glm::vec2(1, 0), glm::vec2(0, 1), quad[1] - quad[0], quad[2] - quad[1]}) {
    glm::vec2 a(1e30f, -1e30f), b(1e30f, -1e30f);  // each one's span along the axis
    for (int i = 0; i < 4; ++i) {
      const float p = glm::dot(quad[i], axis), q = glm::dot(box[i], axis);
      a = {std::min(a.x, p), std::max(a.y, p)};
      b = {std::min(b.x, q), std::max(b.y, q)};
    }
    if (a.y < b.x || b.y < a.x) return false;
  }
  return true;
}

bool inside(glm::vec2 p, glm::vec2 lo, glm::vec2 hi) { return p.x >= lo.x && p.y >= lo.y && p.x <= hi.x && p.y <= hi.y; }

}  // namespace

void MomentsModule::initialize(Engine& app) {
  _app = &app;
  _run = dynamic_cast<AgentRun*>(&app.agent());
  _renderer = app.getModules().find<Renderer2DModule>();
  _ui = app.getModules().find<UIModule>();
  if (!_run || !_renderer || !_ui) return;
  _renderer->addOverlayPass([this](Renderer2D& renderer) { paint(renderer); });
  _run->setDescriber([this](const Pick& pick) { return describe(pick); });
}

glm::vec2 MomentsModule::toWorld(glm::vec2 point) const {
  const Camera2D& camera = _renderer->renderer().camera();  // as last drawn: shake and blending included
  const glm::vec2 half = glm::vec2(_renderer->renderer().logicalSize()) * 0.5f;
  return camera.position() + glm::vec2(point.x - half.x, half.y - point.y) / camera.zoom();
}

std::optional<std::array<glm::vec2, 2>> MomentsModule::screenBox(const Pick& pick) const {
  Renderer2D& r = _renderer->renderer();
  std::vector<glm::vec2> corners{pick.lo, pick.hi};
  if (pick.kind == Pick::Kind::Entity) {
    const auto quad = quadOf(pick.entity);
    if (!quad) return std::nullopt;
    corners.assign(quad->begin(), quad->end());
  }
  std::array<glm::vec2, 2> box{glm::vec2(1e30f), glm::vec2(-1e30f)};
  for (const glm::vec2 world : corners) {
    box[0] = glm::min(box[0], DebugDraw::toScreen(r, world));
    box[1] = glm::max(box[1], DebugDraw::toScreen(r, world));
  }
  return box;
}

nlohmann::json MomentsModule::describe(const Pick& pick) const {
  nlohmann::json out = nlohmann::json::object();
  Renderer2D& r = _renderer->renderer();
  if (const auto box = screenBox(pick)) {
    // What shows of it: the game's area, never the letterbox bars; a point only inside it.
    const glm::vec2 lo = glm::max((*box)[0], glm::vec2(0.0f)), hi = glm::min((*box)[1], glm::vec2(r.logicalSize()));
    const bool shows = pick.kind == Pick::Kind::Point ? lo == (*box)[0] && hi == (*box)[1] : hi.x > lo.x && hi.y > lo.y;
    // Logical px, then the frame's: the letterboxed game area (x, y from the bottom-left) inside it.
    const glm::vec4 game = r.gameViewport();
    const glm::vec2 frame(r.frameSize()), scale = glm::vec2(game.z, game.w) / glm::vec2(r.logicalSize());
    const glm::vec2 corner(game.x, frame.y - game.y - game.w);
    const glm::vec2 a = (corner + lo * scale) / frame, b = (corner + hi * scale) / frame;
    auto round = [](float v) { return std::round(static_cast<double>(v) * 10000.0) / 10000.0; };
    if (shows) out["rect"] = {round(a.x), round(a.y), round(b.x - a.x), round(b.y - a.y)};
  }
  if (pick.kind == Pick::Kind::Entity) return out;
  // What's there: the entities drawn under the point (topmost first) or touching the area, then its tiles.
  World& world = _app->getWorld();
  std::vector<EntityId> there;
  if (pick.kind == Pick::Kind::Point) {
    there = under(DebugDraw::toScreen(r, pick.lo));
  } else {
    for (const EntityId id : world.entities()) {
      const auto quad = quadOf(id);
      if (!quad || world.isPendingDestroy(id)) continue;
      if (touches(*quad, pick.lo, pick.hi)) there.push_back(id);
    }
  }
  nlohmann::json entities = nlohmann::json::array();
  for (size_t i = 0; i < std::min(there.size(), kListed); ++i) {
    entities.push_back({{"entity", idJson(there[i])}, {"name", nameOf(there[i])}, {"origin", _run->originOf(there[i])}});
  }
  out["entities"] = std::move(entities);
  if (there.size() > kListed) out["moreEntities"] = there.size() - kListed;
  out["tiles"] = tilesIn(pick.lo, pick.hi);
  return out;
}

nlohmann::json MomentsModule::tilesIn(glm::vec2 lo, glm::vec2 hi) const {
  nlohmann::json maps = nlohmann::json::array();
  const glm::vec2 camera = _renderer->renderer().camera().position();
  for (auto [id, map, transform] : _app->getWorld().view<TileMapComponent, TransformComponent>()) {
    const TileGrid& grid = map->grid;
    // The cells a box covers, from a layer's bottom-left; none when it's off the map.
    auto cells = [&](glm::vec2 base) -> std::optional<std::array<glm::ivec2, 2>> {
      const glm::ivec2 a = glm::max(grid.tileOf(lo - base), glm::ivec2(0));
      const glm::ivec2 b = glm::min(grid.tileOf(hi - base), glm::ivec2(grid.width() - 1, grid.height() - 1));
      if (b.x < a.x || b.y < a.y) return std::nullopt;
      return std::array{a, b};
    };
    const glm::vec2 origin(transform->position);
    const auto range = cells(origin);  // the map's own; a layer's offset or parallax moves its cells
    // As TileMapRenderModule draws them: Tiled's parallax pivots on the map's top-left (or its "parallaxorigin").
    const glm::vec2 pivot = origin + glm::vec2(0.0f, grid.pixelSize().y) +
                            glm::vec2(grid.properties().value("parallaxoriginx", 0.0f), -grid.properties().value("parallaxoriginy", 0.0f));
    nlohmann::json layers = nlohmann::json::array();
    for (const TileLayer& layer : grid.layers()) {
      const auto in = layer.visible ? cells(origin + layer.offset + (camera - pivot) * (1.0f - layer.parallax)) : std::nullopt;
      if (!in) continue;
      nlohmann::json found = nlohmann::json::array(), types = nlohmann::json::object();
      size_t count = 0;
      for (int ty = (*in)[0].y; ty <= (*in)[1].y; ++ty) {
        for (int tx = (*in)[0].x; tx <= (*in)[1].x; ++tx) {
          const uint32_t gid = grid.gidAt(layer, tx, ty) & ~gid::Flags;
          if (gid == 0) continue;
          if (++count <= kCells) found.push_back({tx, ty, gid});
          const TileGrid::Resolved tile = grid.resolve(gid);
          const Tile* t = tile.tileset ? tile.tileset->tile(tile.id) : nullptr;
          if (t && !t->type.empty()) types[t->type] = types.value(t->type, 0) + 1;
        }
      }
      if (count == 0) continue;
      layers.push_back({{"layer", layer.name}, {"cells", std::move(found)}, {"count", count}});
      if (!types.empty()) layers.back()["types"] = std::move(types);
    }
    if (!range && layers.empty()) continue;
    maps.push_back({{"map", grid.path()}, {"entity", idJson(id)}, {"layers", std::move(layers)}});
    if (range) maps.back().update({{"from", {(*range)[0].x, (*range)[0].y}}, {"to", {(*range)[1].x, (*range)[1].y}}});
  }
  return maps;
}

std::string MomentsModule::nameOf(EntityId id) const {
  // The scene's ("name" is its first tag), else the prefab it was spawned from.
  const auto tags = _app->getWorld().tagNames(id);
  if (!tags.empty()) return tags.front();
  const nlohmann::json origin = _run->originOf(id);
  if (origin.value("prefab", nlohmann::json()).is_string()) {
    std::string prefab = std::filesystem::path(origin["prefab"].get<std::string>()).filename().string();
    if (prefab.ends_with(".prefab.json")) prefab.resize(prefab.size() - std::string_view(".prefab.json").size());
    return prefab;
  }
  return "entity " + std::to_string(id.index);
}

std::optional<std::array<glm::vec2, 4>> MomentsModule::quadOf(EntityId id) const {
  World& world = _app->getWorld();
  const auto* t = world.getComponent<TransformComponent>(id);
  if (!t || world.hasComponent<TileMapComponent>(id)) return std::nullopt;
  glm::vec2 center(t->position), half(0.0f);
  float turn = 0.0f;
  if (world.hasComponent<SpriteComponent>(id)) {  // scale is its half size
    half = glm::abs(t->scale);
    turn = t->rotationRad;
  } else if (const auto* box = world.getComponent<BoxColliderComponent>(id)) {
    center += box->offset;
    half = box->halfExtents;
  } else if (const auto* circle = world.getComponent<CircleColliderComponent>(id)) {
    center += circle->offset;
    half = glm::vec2(circle->radius);
  } else {
    return std::nullopt;
  }
  const glm::vec2 x(std::cos(turn), std::sin(turn)), y(-x.y, x.x);
  return std::array<glm::vec2, 4>{center - x * half.x - y * half.y, center + x * half.x - y * half.y,
                                  center + x * half.x + y * half.y, center - x * half.x + y * half.y};
}

std::vector<EntityId> MomentsModule::under(glm::vec2 point) const {
  const glm::vec2 size(_renderer->renderer().logicalSize());
  if (point.x < 0 || point.y < 0 || point.x >= size.x || point.y >= size.y) return {};  // a letterbox bar
  World& world = _app->getWorld();
  const glm::vec2 at = toWorld(point);
  struct Hit {
    bool sprite;
    float z;
    size_t order;  // drawn later: on top of an equal z
    EntityId id;
  };
  std::vector<Hit> hits;
  for (const EntityId id : world.entities()) {
    const auto quad = quadOf(id);
    if (!quad || world.isPendingDestroy(id)) continue;
    bool inside = true;  // on the inner side of every edge
    for (int i = 0; i < 4 && inside; ++i) {
      const glm::vec2 e = (*quad)[(i + 1) % 4] - (*quad)[i], p = at - (*quad)[i];
      inside = e.x * p.y - e.y * p.x >= 0.0f;
    }
    if (inside) {
      hits.push_back({world.hasComponent<SpriteComponent>(id), world.getComponent<TransformComponent>(id)->position.z,
                      hits.size(), id});
    }
  }
  std::ranges::sort(hits, [](const Hit& a, const Hit& b) {
    return std::tie(a.sprite, a.z, a.order) > std::tie(b.sprite, b.z, b.order);
  });
  std::vector<EntityId> out;
  for (const Hit& hit : hits) out.push_back(hit.id);
  return out;
}

void MomentsModule::click(glm::vec2 at, bool add) {
  // Shift on a point or an area's edge takes it away.
  for (const Pick& pick : add ? _run->picks() : std::vector<Pick>{}) {
    const auto box = pick.kind == Pick::Kind::Entity ? std::nullopt : screenBox(pick);
    if (!box) continue;
    const bool onEdge = inside(at, (*box)[0] - kNear, (*box)[1] + kNear) &&
                        (pick.kind == Pick::Kind::Point || !inside(at, (*box)[0] + kNear, (*box)[1] - kNear));
    if (onEdge) return _run->pick(pick, true);
  }
  const auto hits = under(at);
  if (!hits.empty() || !inside(at, glm::vec2(0.0f), glm::vec2(_renderer->renderer().logicalSize()))) {
    return _run->pick({.entity = hits.empty() ? kNoEntityId : hits.front()}, add);  // a letterbox bar: nothing
  }
  const glm::vec2 world = toWorld(at);
  _run->pick({.kind = Pick::Kind::Point, .lo = world, .hi = world}, add);
}

void MomentsModule::drag(glm::vec2 a, glm::vec2 b, bool add) {
  // The game's area of it, in the world.
  const glm::vec2 size(_renderer->renderer().logicalSize());
  a = glm::clamp(a, glm::vec2(0.0f), size);
  b = glm::clamp(b, glm::vec2(0.0f), size);
  if (a.x == b.x || a.y == b.y) return;  // all in a letterbox bar
  const glm::vec2 p = toWorld(a), q = toWorld(b);
  _run->pick({.kind = Pick::Kind::Area, .lo = glm::min(p, q), .hi = glm::max(p, q)}, add);
}

void MomentsModule::draw(Engine& app, float) {
  const bool held = _run && _run->holding();
  GLFWwindow* window = app.embedded() || app.getDevOptions().headless ? nullptr : glfwGetCurrentContext();
  if (!held || !window) {
    _wasHeld = false;
    _pressed.reset();
    return;
  }
  auto key = [window](int k) { return glfwGetKey(window, k) == GLFW_PRESS; };
  const Keys now{key(GLFW_KEY_F8), key(GLFW_KEY_ESCAPE), key(GLFW_KEY_ENTER) || key(GLFW_KEY_KP_ENTER),
                 key(GLFW_KEY_PERIOD), glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS};
  const Keys was = std::exchange(_was, now);
  if (!std::exchange(_wasHeld, true)) return;  // F8 is still down from holding: only what's pressed from now counts
  if ((now.f8 && !was.f8) || (now.esc && !was.esc)) return _run->resume();
  if (now.enter && !was.enter) {
    _run->saveMoment();
    return;
  }
  if (now.period && !was.period) _run->stepHeld();
  // The window's coordinates in framebuffer px (2x on HiDPI), then the game's logical px.
  double x = 0, y = 0;
  int ww = 0, wh = 0, fw = 0, fh = 0;
  glfwGetCursorPos(window, &x, &y);
  glfwGetWindowSize(window, &ww, &wh);
  glfwGetFramebufferSize(window, &fw, &fh);
  const glm::vec2 px(static_cast<float>(x) * (ww > 0 ? static_cast<float>(fw) / ww : 1.0f),
                     static_cast<float>(y) * (wh > 0 ? static_cast<float>(fh) / wh : 1.0f));
  const Renderer2D& r = _renderer->renderer();
  _pointer = letterbox::toLogical(px, r.gameViewport(), r.frameSize().y, r.logicalSize().x);
  if (now.click && !was.click) _pressed = _pointer;
  if (!now.click && was.click && _pressed) {  // let go: a click where it went down, or a drag to here
    const bool add = key(GLFW_KEY_LEFT_SHIFT) || key(GLFW_KEY_RIGHT_SHIFT);
    if (glm::length(_pointer - *_pressed) > kDrag) drag(*_pressed, _pointer, add);
    else click(*_pressed, add);
    _pressed.reset();
  }
}

bool MomentsModule::driveCommand(Engine& app, std::string_view verb, std::string_view args, nlohmann::json& reply) {
  if ((verb != "pick" && verb != "area") || !_run || !_renderer) return false;
  std::istringstream in{std::string(args)};
  const size_t count = verb == "pick" ? 2 : 4;
  float at[4] = {};
  std::string add, extra;
  bool read = true;
  for (size_t i = 0; i < count; ++i) read = read && static_cast<bool>(in >> at[i]);
  in >> add >> extra;
  if (!read || (!add.empty() && add != "add") || !extra.empty()) {
    reply = failure(verb == "pick" ? "pick takes x y in logical px, and add to add or take away: pick 120 80 add"
                                   : "area takes x0 y0 x1 y1 in logical px (two corners), and add to add: area 40 30 200 120 add");
  } else if (!_run->holding()) {
    reply = failure(std::string(verb) + ": not frozen (freeze first)");
  } else {
    const auto hits = verb == "pick" ? under({at[0], at[1]}) : std::vector<EntityId>{};
    if (verb == "pick") click({at[0], at[1]}, add == "add");
    else drag({at[0], at[1]}, {at[2], at[3]}, add == "add");
    nlohmann::json picks = nlohmann::json::array();
    for (const Pick& pick : _run->picks()) picks.push_back(AgentRun::pickJson(pick));
    reply = {{"ok", true}, {"picks", picks}};
    if (verb == "pick") reply["under"] = hits.empty() ? nlohmann::json() : idJson(hits.front());
    if (app.getDevOptions().drive) app.frame(0.0f);  // shown: a driven run draws only when told
  }
  return true;
}

void MomentsModule::paint(Renderer2D& renderer) {
  if (!_run->holding() || _run->drawsClean()) return;  // the clean frame is the saved moment's picture
  _marks.clear();
  auto box = [&](glm::vec2 lo, glm::vec2 hi) {
    _marks.add(DebugDraw::Shape::Rect, true, (lo + hi) * 0.5f, hi - lo, kPicked, 0.0f, false);
  };
  const auto picks = _run->picks();
  for (size_t i = 0; i < picks.size(); ++i) {
    const Pick& pick = picks[i];
    const auto screen = screenBox(pick);
    if (!screen) continue;
    glm::vec2 top = (*screen)[0];
    std::string label = std::to_string(i + 1) + " ";
    if (pick.kind == Pick::Kind::Entity) {  // its outline, turned as it's drawn
      const auto quad = *quadOf(pick.entity);
      for (int c = 0; c < 4; ++c) {
        _marks.add(DebugDraw::Shape::Line, true, DebugDraw::toScreen(renderer, quad[c]),
                   DebugDraw::toScreen(renderer, quad[(c + 1) % 4]), kPicked, 0.0f, false);
      }
      label += nameOf(pick.entity);
    } else if (pick.kind == Pick::Kind::Point) {  // a crosshair in a ring
      const glm::vec2 at = top;
      _marks.add(DebugDraw::Shape::Circle, true, at, glm::vec2(kNear * 0.8f), kPicked, 0.0f, false);
      _marks.add(DebugDraw::Shape::Line, true, at - glm::vec2(kNear * 1.6f, 0), at + glm::vec2(kNear * 1.6f, 0), kPicked, 0.0f, false);
      _marks.add(DebugDraw::Shape::Line, true, at - glm::vec2(0, kNear * 1.6f), at + glm::vec2(0, kNear * 1.6f), kPicked, 0.0f, false);
      top -= glm::vec2(kNear, kNear * 1.6f);
      label += "point";
    } else {
      box((*screen)[0], (*screen)[1]);
      label += "area";
    }
    _marks.add(DebugDraw::Shape::Text, true, top - glm::vec2(0.0f, 7.0f), {}, kPicked, 0.0f, false, std::move(label));
  }
  if (_pressed && glm::length(_pointer - *_pressed) > kDrag) box(glm::min(*_pressed, _pointer), glm::max(*_pressed, _pointer));
  // The badge, and a frame around the game: the run is held, not the game paused.
  const glm::vec2 size(renderer.logicalSize());
  const glm::vec4 border(1.0f, 0.82f, 0.25f, 0.9f);
  for (const glm::vec4 edge : {glm::vec4(0, 0, size.x, 2), glm::vec4(0, size.y - 2, size.x, 2),
                               glm::vec4(0, 0, 2, size.y), glm::vec4(size.x - 2, 0, 2, size.y)}) {
    renderer.drawScreenQuad(edge, border, glm::vec4(0, 0, 1, 1), renderer.whiteTexture());
  }
  // Bottom left: games keep their HUD at the top.
  renderer.drawScreenQuad({4, size.y - 18, std::min(size.x - 8, 212.0f), 14}, {0.05f, 0.05f, 0.08f, 0.85f},
                          glm::vec4(0, 0, 1, 1), renderer.whiteTexture());
  _marks.add(DebugDraw::Shape::Text, true, {8, size.y - 11}, {}, kPicked, 0.0f, false, kBadge);
  _ui->paintDebug(renderer, _marks);
}
