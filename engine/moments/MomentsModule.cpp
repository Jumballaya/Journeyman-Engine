#include "MomentsModule.hpp"
#include "PickTags.hpp"

#include <algorithm>
#include <cctype>
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
#include "../ui/Text.hpp"
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
constexpr const char* kHelp = "Paused · F8 · Enter saves";
constexpr const char* kTyping = "Enter keeps · Esc undoes";
constexpr uint32_t kPlaceholder = 0x9AA0B0;  // "type a note"
constexpr glm::vec4 kFocused(1.0f, 0.82f, 0.25f, 1.0f);  // kPicked: the label typing writes
constexpr float kBadgeHeight = 14.0f;
constexpr float kLine = 10.0f;      // a line of the debug text (8 px, 1.25 high)
constexpr float kPad = 2.0f;        // around a label's text
constexpr float kNoteWidth = 120.0f;  // a note's lines, at most
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
  focus(_focus);  // what's typed so far is kept
  // On a pick (a point, an area's edge, an entity picked): shift takes it
  // away, a click is to write its note.
  const auto picks = _run->picks();
  const auto hits = under(at);
  for (size_t i = 0; i < picks.size(); ++i) {
    const Pick& pick = picks[i];
    const auto box = pick.kind == Pick::Kind::Entity ? std::nullopt : screenBox(pick);
    const bool on = box ? inside(at, (*box)[0] - kNear, (*box)[1] + kNear) &&
                              (pick.kind == Pick::Kind::Point || !inside(at, (*box)[0] + kNear, (*box)[1] - kNear))
                        : !hits.empty() && pick.entity == hits.front();
    if (!on) continue;
    if (!add) return focus(i + 1);
    _run->pick(pick, true);
    return focus(_run->picks().size());
  }
  if (!hits.empty() || !inside(at, glm::vec2(0.0f), glm::vec2(_renderer->renderer().logicalSize()))) {
    _run->pick({.entity = hits.empty() ? kNoEntityId : hits.front()}, add);  // a letterbox bar: nothing
  } else {
    const glm::vec2 world = toWorld(at);
    _run->pick({.kind = Pick::Kind::Point, .lo = world, .hi = world}, add);
  }
  focus(_run->picks().size());  // the latest
}

void MomentsModule::drag(glm::vec2 a, glm::vec2 b, bool add) {
  focus(_focus);
  // The game's area of it, in the world.
  const glm::vec2 size(_renderer->renderer().logicalSize());
  a = glm::clamp(a, glm::vec2(0.0f), size);
  b = glm::clamp(b, glm::vec2(0.0f), size);
  if (a.x == b.x || a.y == b.y) return;  // all in a letterbox bar
  const glm::vec2 p = toWorld(a), q = toWorld(b);
  _run->pick({.kind = Pick::Kind::Area, .lo = glm::min(p, q), .hi = glm::max(p, q)}, add);
  focus(_run->picks().size());
}

void MomentsModule::focus(size_t n) {
  if (_edit.editing()) _run->setNote(_focus, _edit.commit());
  _focus = n;
}

void MomentsModule::type(const Typed& typed) {
  if (typed.ch != 0) {
    if (!_edit.editing() && typed.ch == U'.') return;  // its key steps
    if (!_edit.editing()) _edit.begin(_run->noteOf(_focus));
    return _edit.type(typed.ch);
  }
  if (typed.repeat && typed.key != GLFW_KEY_BACKSPACE) return;
  switch (typed.key) {
    case GLFW_KEY_F8:
      return _run->resume();
    case GLFW_KEY_ESCAPE:
      if (_edit.editing()) _edit.cancel();
      else _run->resume();
      return;
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER:
      if (_edit.editing()) _run->setNote(_focus, _edit.commit());
      else _run->saveMoment();
      return;
    case GLFW_KEY_TAB: {  // the next pick's note (shift: the one before), round through the moment's
      const size_t notes = _run->picks().size() + 1;
      return focus((_focus + ((typed.mods & GLFW_MOD_SHIFT) ? notes - 1 : 1)) % notes);
    }
    case GLFW_KEY_BACKSPACE:
      if (!_edit.editing()) _edit.begin(_run->noteOf(_focus));
      return _edit.erase();
    case GLFW_KEY_PERIOD:
      if (!_edit.editing() && typed.mods == 0) _run->stepHeld();
      return;
  }
}

void MomentsModule::onKey(GLFWwindow* window, int key, int scancode, int action, int mods) {
  MomentsModule* self = static_cast<Engine*>(glfwGetWindowUserPointer(window))->getModules().find<MomentsModule>();
  const bool held = self->_run->holding();  // before the game's: F8 down holds the run there
  if (self->_passKey) self->_passKey(window, key, scancode, action, mods);
  if (held && action != GLFW_RELEASE) self->_typed.push_back({.key = key, .mods = mods, .repeat = action == GLFW_REPEAT});
}

void MomentsModule::onChar(GLFWwindow* window, unsigned int ch) {
  MomentsModule* self = static_cast<Engine*>(glfwGetWindowUserPointer(window))->getModules().find<MomentsModule>();
  if (self->_passChar) self->_passChar(window, ch);
  if (self->_run->holding()) self->_typed.push_back({.ch = static_cast<char32_t>(ch)});
}

void MomentsModule::draw(Engine& app, float) {
  const bool held = _run && _run->holding();
  GLFWwindow* window = app.embedded() || app.getDevOptions().headless ? nullptr : glfwGetCurrentContext();
  if (!held) {  // a hold starts with no note typed: the moment's is the one to write
    _edit.cancel();
    _focus = 0;
  }
  if (!held || !window) {
    _wasHeld = false;
    _pressed.reset();
    _typed.clear();
    return;
  }
  if (!std::exchange(_listening, true)) {  // the window's keys and characters, as they come
    _passKey = glfwSetKeyCallback(window, onKey);
    _passChar = glfwSetCharCallback(window, onChar);
  }
  const bool clicked = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  const bool wasClicked = std::exchange(_wasClicked, clicked);
  if (!std::exchange(_wasHeld, true)) return _typed.clear();  // only what's pressed from now counts
  for (const Typed& typed : std::exchange(_typed, {})) {
    if (!_run->holding()) return;  // saved, or going on
    type(typed);
  }
  if (!_run->holding()) return;
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
  if (clicked && !wasClicked) _pressed = _pointer;
  if (!clicked && wasClicked && _pressed) {  // let go: a click where it went down, or a drag to here
    const bool add = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    if (glm::length(_pointer - *_pressed) > kDrag) drag(*_pressed, _pointer, add);
    else click(*_pressed, add);
    _pressed.reset();
  }
}

bool MomentsModule::driveCommand(Engine& app, std::string_view verb, std::string_view args, nlohmann::json& reply) {
  if ((verb != "pick" && verb != "area" && verb != "note") || !_run || !_renderer) return false;
  auto picked = [this] {
    nlohmann::json picks = nlohmann::json::array();
    for (const Pick& pick : _run->picks()) picks.push_back(AgentRun::pickJson(pick));
    return picks;
  };
  if (!_run->holding()) {
    reply = failure(std::string(verb) + ": not frozen (freeze first)");
  } else if (verb == "note") {  // `note [n] <text>`
    size_t n = 0, digits = 0;
    while (digits < args.size() && digits < 7 && std::isdigit(static_cast<unsigned char>(args[digits]))) n = n * 10 + (args[digits++] - '0');
    if (digits > 0 && digits < args.size() && !std::isspace(static_cast<unsigned char>(args[digits]))) n = digits = 0;  // "3d is wrong": all note
    NoteEdit note;  // as if typed: what the keyboard can't type isn't kept
    note.begin(std::string(args.substr(std::min(digits + (digits > 0), args.size()))));
    focus(_focus);
    if (_run->setNote(n, note.commit())) reply = {{"ok", true}, {"note", _run->noteOf(0)}, {"picks", picked()}};
    else reply = failure("note: no pick " + std::to_string(n) + " (" + std::to_string(_run->picks().size()) + " picked)");
  } else {
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
      return true;
    }
    const auto hits = verb == "pick" ? under({at[0], at[1]}) : std::vector<EntityId>{};
    if (verb == "pick") click({at[0], at[1]}, add == "add");
    else drag({at[0], at[1]}, {at[2], at[3]}, add == "add");
    reply = {{"ok", true}, {"picks", picked()}};
    if (verb == "pick") reply["under"] = hits.empty() ? nlohmann::json() : idJson(hits.front());
  }
  if (reply["ok"] == true && app.getDevOptions().drive) app.frame(0.0f);  // shown: a driven run draws only when told
  return true;
}

std::vector<std::string> MomentsModule::noteLines(const std::string& note, bool typing, float width, size_t most) {
  // Word by word; a word wider than a line, a code point at a time.
  std::vector<std::string> lines{std::string()};
  const std::string text = typing ? note + "_" : note;
  for (size_t i = 0; i < text.size();) {
    const size_t start = i;
    nextCodepoint(text, i);
    const std::string piece = text.substr(start, i - start);
    std::string& line = lines.back();
    if (piece == " " && line.empty()) continue;
    if (_ui->debugTextWidth(line + piece) <= width || line.empty()) {
      line += piece;
      continue;
    }
    const size_t space = line.rfind(' ');
    std::string carried = space == std::string::npos || piece == " " ? std::string() : line.substr(space + 1);
    if (!carried.empty()) line.resize(space);
    lines.push_back(piece == " " ? carried : carried + piece);
  }
  if (lines.size() == 1 && lines[0].empty()) return {};
  if (lines.size() <= most) return lines;
  if (typing) {  // its end, where the caret is
    lines.erase(lines.begin(), lines.end() - static_cast<ptrdiff_t>(most));
    lines.front() = "..." + lines.front();
  } else {
    lines.resize(most);
    lines.back() += "...";
  }
  return lines;
}

glm::vec4 MomentsModule::label(Renderer2D& renderer, const Label& label, std::vector<glm::vec4>& taken) {
  const glm::vec2 size(renderer.logicalSize());
  float width = 0.0f;
  for (const std::string& line : label.lines) width = std::max(width, _ui->debugTextWidth(line) + 1.0f);  // and its shadow
  const glm::vec2 tag(width + 2 * kPad, static_cast<float>(label.lines.size()) * kLine + kPad);
  // Beside what it marks (above, below, right, left, inside an area), then
  // further up; inside the game, over the badge: the first place clear of the
  // marks, the other labels and the badge, else the one least on them.
  const glm::vec2 lo = label.lo, hi = label.hi;
  const float gap = 2.0f, lowest = size.y - kBadgeHeight - 6.0f - tag.y;
  std::vector<glm::vec2> places{{lo.x, lo.y - gap - tag.y}, {lo.x, hi.y + gap}, {hi.x - tag.x, lo.y - gap - tag.y},
                                {hi.x - tag.x, hi.y + gap},  {hi.x + gap, lo.y},  {lo.x - gap - tag.x, lo.y},
                                {lo.x + gap, lo.y + gap}};
  for (float up = lo.y - gap - tag.y - kLine; up > -kLine; up -= kLine) places.insert(places.end(), {{lo.x, up}, {hi.x - tag.x, up}});
  places.insert(places.end(), {{size.x - tag.x, lowest}, {0.0f, 0.0f}, {size.x, 0.0f}});
  glm::vec4 best(0.0f);
  float least = 1e30f;
  for (const glm::vec2 at : places) {
    const glm::vec4 place(std::clamp(at.x, 4.0f, std::max(4.0f, size.x - tag.x - 4.0f)), std::clamp(at.y, 4.0f, std::max(4.0f, lowest)), tag);
    float on = 0.0f;
    for (const glm::vec4& t : taken) {
      on += std::max(0.0f, std::min(place.x + place.z, t.x + t.z) - std::max(place.x, t.x)) *
            std::max(0.0f, std::min(place.y + place.w, t.y + t.w) - std::max(place.y, t.y));
    }
    if (on < least) std::tie(best, least) = std::tuple(place, on);
    if (on == 0.0f) break;
  }
  taken.push_back(best);
  renderer.drawScreenQuad(best, {0.05f, 0.05f, 0.08f, 0.88f}, glm::vec4(0, 0, 1, 1), renderer.whiteTexture());
  if (label.focused) renderer.drawScreenQuad({best.x, best.y + tag.y - 1.0f, tag.x, 1.0f}, kFocused, glm::vec4(0, 0, 1, 1), renderer.whiteTexture());
  _labels.clear();  // drawn now: over the labels before it, under the ones after
  for (size_t i = 0; i < label.lines.size(); ++i) {
    const glm::vec2 at(best.x + kPad, best.y + kPad * 0.5f + kLine * (static_cast<float>(i) + 0.5f));
    const bool heading = i == 0 && !label.numbered;
    _labels.add(DebugDraw::Shape::Text, true, at, {}, heading ? kPicked : label.placeholder ? kPlaceholder : 0xFFFFFF, 0.0f, false, label.lines[i]);
    if (label.numbered) _labels.add(DebugDraw::Shape::Text, true, at, {}, kPicked, 0.0f, false, label.lines[i].substr(0, label.lines[i].find(' ')));
  }
  _ui->paintDebug(renderer, _labels);
  return best;
}

void MomentsModule::tag(Renderer2D& renderer, glm::vec4 box, const std::string& text, bool lit) {
  renderer.drawScreenQuad(box, {0.05f, 0.05f, 0.08f, 0.92f}, glm::vec4(0, 0, 1, 1), renderer.whiteTexture());
  if (lit) renderer.drawScreenQuad({box.x, box.y + box.w - 1.0f, box.z, 1.0f}, kFocused, glm::vec4(0, 0, 1, 1), renderer.whiteTexture());
  _labels.clear();
  _labels.add(DebugDraw::Shape::Text, true, {box.x + kPad, box.y + box.w * 0.5f}, {}, kPicked, 0.0f, false, text);
  _ui->paintDebug(renderer, _labels);
}

void MomentsModule::paint(Renderer2D& renderer) {
  if (!_run->holding() || _run->drawsClean()) return;  // the clean frame is the saved moment's picture
  _marks.clear();
  auto box = [&](glm::vec2 lo, glm::vec2 hi) {
    _marks.add(DebugDraw::Shape::Rect, true, (lo + hi) * 0.5f, hi - lo, kPicked, 0.0f, false);
  };
  const glm::vec2 size(renderer.logicalSize());
  const float width = std::min(kNoteWidth, size.x * 0.45f);
  const auto picks = _run->picks();
  _focus = std::min(_focus, picks.size());
  // A note's lines: typed (its end, a caret), kept, or where to type one.
  auto note = [&](size_t n, Label& label, size_t most) {
    const bool focused = _focus == n, typing = focused && _edit.editing();
    const std::string text = typing ? _edit.text() : n == 0 ? _run->noteOf(0) : picks[n - 1].note;
    auto lines = noteLines(text, typing, width, most);
    label.placeholder = lines.empty() && focused;
    if (label.placeholder) lines = {"type a note"};
    label.lines.insert(label.lines.end(), lines.begin(), lines.end());
    label.focused = focused;
  };
  struct Pin {
    Pick::Kind kind;
    glm::vec2 lo, hi;
    std::string name;
  };
  std::vector<std::optional<Pin>> pins;  // each pick's mark, if it's drawn
  std::vector<glm::vec4> taken;  // what labels keep off: the marks, the numbers, the badge
  for (size_t i = 0; i < picks.size(); ++i) {
    const Pick& pick = picks[i];
    const auto screen = screenBox(pick);
    pins.emplace_back();
    if (!screen) continue;
    auto [lo, hi] = *screen;
    std::string name;
    if (pick.kind == Pick::Kind::Entity) {  // its outline, turned as it's drawn
      const auto quad = *quadOf(pick.entity);
      for (int c = 0; c < 4; ++c) {
        _marks.add(DebugDraw::Shape::Line, true, DebugDraw::toScreen(renderer, quad[c]),
                   DebugDraw::toScreen(renderer, quad[(c + 1) % 4]), kPicked, 0.0f, false);
      }
      name = nameOf(pick.entity);
    } else if (pick.kind == Pick::Kind::Point) {  // a crosshair in a ring
      const glm::vec2 at = lo, arm(kNear * 1.6f);
      _marks.add(DebugDraw::Shape::Circle, true, at, glm::vec2(kNear * 0.8f), kPicked, 0.0f, false);
      _marks.add(DebugDraw::Shape::Line, true, at - glm::vec2(arm.x, 0), at + glm::vec2(arm.x, 0), kPicked, 0.0f, false);
      _marks.add(DebugDraw::Shape::Line, true, at - glm::vec2(0, arm.y), at + glm::vec2(0, arm.y), kPicked, 0.0f, false);
      lo = at - arm;
      hi = at + arm;
      name = "point";
    } else {
      box(lo, hi);
      name = "area";
    }
    // An area's inside is for labels: only its edges are kept clear.
    if (pick.kind == Pick::Kind::Area) {
      for (const glm::vec4 edge : {glm::vec4(lo.x, lo.y - 1, hi.x - lo.x, 2), glm::vec4(lo.x, hi.y - 1, hi.x - lo.x, 2),
                                   glm::vec4(lo.x - 1, lo.y, 2, hi.y - lo.y), glm::vec4(hi.x - 1, lo.y, 2, hi.y - lo.y)})
        taken.push_back(edge);
    } else {
      taken.emplace_back(lo, hi - lo);
    }
    pins.back() = Pin{pick.kind, lo, hi, name};
  }
  if (_pressed && glm::length(_pointer - *_pressed) > kDrag) box(glm::min(*_pressed, _pointer), glm::max(*_pressed, _pointer));
  _ui->paintDebug(renderer, _marks);  // the marks first: labels and the badge go over them
  // A frame around the game: the run is held, not the game paused.
  const glm::vec4 border(1.0f, 0.82f, 0.25f, 0.9f);
  for (const glm::vec4 edge : {glm::vec4(0, 0, size.x, 2), glm::vec4(0, size.y - 2, size.x, 2),
                               glm::vec4(0, 0, 2, size.y), glm::vec4(size.x - 2, 0, 2, size.y)}) {
    renderer.drawScreenQuad(edge, border, glm::vec4(0, 0, 1, 1), renderer.whiteTexture());
  }
  // The badge bottom left: games keep their HUD at the top.
  const char* help = _edit.editing() ? kTyping : kHelp;
  const glm::vec4 badge(4, size.y - kBadgeHeight - 4, std::min(size.x - 8, _ui->debugTextWidth(help) + 9), kBadgeHeight);
  renderer.drawScreenQuad(badge, {0.05f, 0.05f, 0.08f, 0.92f}, glm::vec4(0, 0, 1, 1), renderer.whiteTexture());
  _labels.clear();
  _labels.add(DebugDraw::Shape::Text, true, {8, badge.y + kBadgeHeight * 0.5f}, {}, kPicked, 0.0f, false, help);
  _ui->paintDebug(renderer, _labels);
  taken.push_back(badge);
  // Each pick's number at its mark, always; the focused one's lit.
  for (size_t i = 0; i < pins.size(); ++i) {
    if (!pins[i]) continue;
    const std::string n = std::to_string(i + 1);
    const glm::vec2 box(_ui->debugTextWidth(n) + 1.0f + 2 * kPad, kLine + kPad);
    const glm::vec2 at = pick_tags::numberAt(pins[i]->kind, pins[i]->lo, pins[i]->hi, box, size);
    taken.emplace_back(at, box);
    tag(renderer, taken.back(), n, _focus == i + 1);
    pins[i]->lo = glm::min(pins[i]->lo, at);  // a leader to the pick may end on its number
    pins[i]->hi = glm::max(pins[i]->hi, at + box);
  }
  // The focused pick's name and note beside it, a line to it when it can't touch it.
  if (_focus > 0 && pins[_focus - 1]) {
    const Pin& pin = *pins[_focus - 1];
    Label focused{pin.lo, pin.hi, {std::to_string(_focus) + " " + pin.name}};
    note(_focus, focused, 2);
    const auto [from, to] = pick_tags::leader(label(renderer, focused, taken), pin.lo, pin.hi);
    if (glm::length(to - from) > 3.0f) {
      _labels.clear();
      _labels.add(DebugDraw::Shape::Line, true, from, to, kPicked, 0.0f, false);
      _ui->paintDebug(renderer, _labels);
    }
  }
  // The moment's note, top left (clear of the player, often at the bottom), when there's one or it's what typing writes.
  if (_focus == 0 || !_run->noteOf(0).empty()) {
    Label moment{glm::vec2(4.0f), glm::vec2(4.0f), {"moment"}};
    note(0, moment, 1);
    label(renderer, moment, taken);
  }
  // The other picks' notes, a line each by their numbers, listed bottom right.
  Label others{size - glm::vec2(4.0f, kBadgeHeight + 4.0f), size - glm::vec2(4.0f, kBadgeHeight + 4.0f), {}};
  others.numbered = true;
  for (size_t i = 0; i < picks.size(); ++i) {
    if (i + 1 == _focus || picks[i].note.empty()) continue;
    const std::string n = std::to_string(i + 1) + " ";
    others.lines.push_back(n + noteLines(picks[i].note, false, size.x * 0.6f - _ui->debugTextWidth(n), 1)[0]);
  }
  if (!others.lines.empty()) label(renderer, others, taken);
}
