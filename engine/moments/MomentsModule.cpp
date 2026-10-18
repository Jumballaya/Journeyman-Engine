#include "MomentsModule.hpp"

#include <algorithm>
#include <cmath>
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

nlohmann::json idJson(EntityId id) { return {id.index, id.generation}; }

}  // namespace

void MomentsModule::initialize(Engine& app) {
  _app = &app;
  _run = dynamic_cast<AgentRun*>(&app.agent());
  _renderer = app.getModules().find<Renderer2DModule>();
  _ui = app.getModules().find<UIModule>();
  if (_run && _renderer && _ui) _renderer->addOverlayPass([this](Renderer2D& renderer) { paint(renderer); });
}

std::optional<std::array<glm::vec2, 4>> MomentsModule::quadOf(EntityId id) const {
  World& world = _app->getWorld();
  const auto* t = world.getComponent<TransformComponent>(id);
  if (!t || world.hasComponentNamed(id, "TileMapComponent")) return std::nullopt;
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
  const Camera2D& camera = _renderer->renderer().camera();  // as last drawn: shake and blending included
  const glm::vec2 half = size * 0.5f;
  const glm::vec2 at = camera.position() + glm::vec2(point.x - half.x, half.y - point.y) / camera.zoom();
  struct Hit {
    bool sprite;
    float z;
    uint32_t texture;
    size_t order;  // drawn later: on top of an equal z and texture
    EntityId id;
  };
  std::vector<Hit> hits;
  auto test = [&](EntityId id, bool sprite, uint32_t texture) {
    const auto quad = quadOf(id);
    if (!quad || world.isPendingDestroy(id)) return;
    // Collapsed (a zero scale hides it): nothing to click, and every point is "inside".
    const glm::vec2 w = (*quad)[1] - (*quad)[0], h = (*quad)[3] - (*quad)[0];
    if (glm::dot(w, w) <= 0.0f || glm::dot(h, h) <= 0.0f) return;
    bool inside = true;  // on the inner side of every edge
    for (int i = 0; i < 4 && inside; ++i) {
      const glm::vec2 e = (*quad)[(i + 1) % 4] - (*quad)[i], p = at - (*quad)[i];
      inside = e.x * p.y - e.y * p.x >= 0.0f;
    }
    if (inside) hits.push_back({sprite, world.getComponent<TransformComponent>(id)->position.z, texture, hits.size(), id});
  };
  // Sprites in the order they're drawn (Renderer2DSystem's), so ties go as the renderer's do.
  const uint32_t white = _renderer->renderer().whiteTexture().id;  // what a sprite without one is drawn with
  for (auto [id, sprite, trans] : world.view<SpriteComponent, TransformComponent>())
    test(id, true, sprite->texture.isValid() ? sprite->texture.id : white);
  for (const EntityId id : world.entities())
    if (!world.hasComponent<SpriteComponent>(id)) test(id, false, 0);
  std::ranges::sort(hits, [](const Hit& a, const Hit& b) {  // the renderer's sort: z, then texture, then as drawn
    return std::tie(a.sprite, a.z, a.texture, a.order) > std::tie(b.sprite, b.z, b.texture, b.order);
  });
  std::vector<EntityId> out;
  for (const Hit& hit : hits) out.push_back(hit.id);
  return out;
}

void MomentsModule::draw(Engine& app, float) {
  const bool held = _run && _run->holding();
  GLFWwindow* window = app.embedded() || app.getDevOptions().headless ? nullptr : glfwGetCurrentContext();
  if (!held || !window) {
    _wasHeld = _saveDue = false;
    return;
  }
  if (std::exchange(_saveDue, false)) {  // its picture (held.png) is taken now
    _run->saveMoment();
    return;
  }
  auto key = [window](int k) { return glfwGetKey(window, k) == GLFW_PRESS; };
  const Keys now{key(GLFW_KEY_F8), key(GLFW_KEY_ESCAPE), key(GLFW_KEY_ENTER) || key(GLFW_KEY_KP_ENTER),
                 key(GLFW_KEY_PERIOD), glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS};
  const Keys was = std::exchange(_was, now);
  if (!std::exchange(_wasHeld, true)) return;  // F8 is still down from holding: only what's pressed from now counts
  if ((now.f8 && !was.f8) || (now.esc && !was.esc)) return _run->resume();
  if (now.enter && !was.enter) {
    // A `.` just ran: held.png is still the frame before, until this one's drawn.
    if (_run->drawsClean()) _saveDue = true;
    else _run->saveMoment();
    return;
  }
  if (now.period && !was.period) _run->stepHeld();
  if (now.click && !was.click) {
    // The window's coordinates in framebuffer px (2x on HiDPI), then the game's logical px.
    double x = 0, y = 0;
    int ww = 0, wh = 0, fw = 0, fh = 0;
    glfwGetCursorPos(window, &x, &y);
    glfwGetWindowSize(window, &ww, &wh);
    glfwGetFramebufferSize(window, &fw, &fh);
    const glm::vec2 px(static_cast<float>(x) * (ww > 0 ? static_cast<float>(fw) / ww : 1.0f),
                       static_cast<float>(y) * (wh > 0 ? static_cast<float>(fh) / wh : 1.0f));
    const Renderer2D& r = _renderer->renderer();
    const auto picked = under(letterbox::toLogical(px, r.gameViewport(), r.frameSize().y, r.logicalSize().x));
    _run->pick(picked.empty() ? kNoEntityId : picked.front(), key(GLFW_KEY_LEFT_SHIFT) || key(GLFW_KEY_RIGHT_SHIFT));
  }
}

bool MomentsModule::driveCommand(Engine& app, std::string_view verb, std::string_view args, nlohmann::json& reply) {
  if (verb != "pick" || !_run || !_renderer) return false;
  std::istringstream in{std::string(args)};
  float x = 0, y = 0;
  std::string add, extra;
  const bool at = static_cast<bool>(in >> x >> y);
  in >> add >> extra;
  if (!at || (!add.empty() && add != "add") || !extra.empty()) {
    reply = {{"ok", false}, {"error", "pick takes x y in logical px, and add to add or take away: pick 120 80 add"}};
  } else if (!_run->holding()) {
    reply = {{"ok", false}, {"error", "pick: not frozen (freeze first)"}};
  } else {
    const auto hits = under({x, y});
    _run->pick(hits.empty() ? kNoEntityId : hits.front(), add == "add");
    nlohmann::json picks = nlohmann::json::array();
    for (const EntityId id : _run->picks()) picks.push_back(idJson(id));
    reply = {{"ok", true}, {"under", hits.empty() ? nlohmann::json() : idJson(hits.front())}, {"picks", picks}};
    if (app.getDevOptions().drive) app.frame(0.0f);  // shown: a driven run draws only when told
  }
  return true;
}

void MomentsModule::paint(Renderer2D& renderer) {
  if (!_run->holding() || _run->drawsClean()) return;  // the clean frame is the saved moment's picture
  World& world = _app->getWorld();
  _marks.clear();
  for (const EntityId id : _run->picks()) {
    const auto quad = quadOf(id);
    if (!quad) continue;
    glm::vec2 top(1e30f);
    for (int i = 0; i < 4; ++i) {
      const glm::vec2 a = DebugDraw::toScreen(renderer, (*quad)[i]), b = DebugDraw::toScreen(renderer, (*quad)[(i + 1) % 4]);
      _marks.add(DebugDraw::Shape::Line, true, a, b, kPicked, 0.0f, false);
      top = glm::min(top, a);
    }
    // Its name: the scene's ("name" is its first tag), else the prefab it was spawned from.
    const auto tags = world.tagNames(id);
    const nlohmann::json origin = _run->originOf(id);
    std::string label = !tags.empty()                    ? tags.front()
                        : origin.value("prefab", nlohmann::json()).is_string() ? origin["prefab"].get<std::string>()
                                                                                : "entity " + std::to_string(id.index);
    _marks.add(DebugDraw::Shape::Text, true, top - glm::vec2(0.0f, 7.0f), {}, kPicked, 0.0f, false, std::move(label));
  }
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
