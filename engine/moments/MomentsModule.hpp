#pragma once

#include <array>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "../core/app/EngineModule.hpp"
#include "../core/app/agent/AgentRun.hpp"
#include "../core/ecs/entity/EntityId.hpp"
#include "../ui/DebugDraw.hpp"

class Renderer2D;
class Renderer2DModule;
class UIModule;

// F8's moment on screen (AgentRun::hold; agent build only: journeyman_engine
// links this, the player doesn't). While the run is held: a click picks the
// entity under the pointer, or the point there when there's none; a drag
// picks the area it spans (whatever it starts on). Shift adds, or takes away
// what's clicked again (a point near it, an area on its edge). The picks are
// outlined and numbered over the frame, under a "Paused · F8" badge; Enter
// saves the moment, F8 or Esc goes on without, `.` steps a frame. The game's
// devices are muted then: this reads the window's itself.
class MomentsModule : public EngineModule {
 public:
  void initialize(Engine& app) override;
  void shutdown(Engine&) override {}
  // The window's keys and mouse, while held.
  void draw(Engine& app, float dt) override;
  // The driver's `pick x y [add]` (a click; add: shift-click) and
  // `area x0 y0 x1 y1 [add]` (a drag), in logical px.
  bool driveCommand(Engine& app, std::string_view verb, std::string_view args, nlohmann::json& reply) override;
  const char* name() const override { return "MomentsModule"; }

  // The entities drawn under a point (logical px from the game's top-left),
  // topmost first: sprites, then colliders of entities without one. Tile maps never.
  std::vector<EntityId> under(glm::vec2 point) const;

 private:
  Engine* _app = nullptr;
  AgentRun* _run = nullptr;
  Renderer2DModule* _renderer = nullptr;
  UIModule* _ui = nullptr;
  DebugDraw _marks;  // the picks' outlines and names, made each held frame
  struct Keys {
    bool f8 = false, esc = false, enter = false, period = false, click = false;
  } _was;  // last frame's, for presses
  bool _wasHeld = false;
  std::optional<glm::vec2> _pressed;  // where the button went down (logical px), while it's down
  glm::vec2 _pointer{0.0f};

  using Pick = AgentRun::Pick;
  // A click at a point, and a drag from a to b (logical px): what they pick.
  void click(glm::vec2 at, bool add);
  void drag(glm::vec2 a, glm::vec2 b, bool add);
  glm::vec2 toWorld(glm::vec2 point) const;  // logical px (y down) to the world (y up)
  // Where an entity is drawn in the world, as a quad (counter-clockwise): its
  // sprite, else its collider's box; null if neither.
  std::optional<std::array<glm::vec2, 4>> quadOf(EntityId id) const;
  // A pick's box on screen (logical px); null for an entity that isn't drawn.
  std::optional<std::array<glm::vec2, 2>> screenBox(const Pick& pick) const;
  // What a saved pick adds: AgentRun::setDescriber.
  nlohmann::json describe(const Pick& pick) const;
  nlohmann::json tilesIn(glm::vec2 lo, glm::vec2 hi) const;  // a world box's tile cells, by map and layer
  std::string nameOf(EntityId id) const;
  void paint(Renderer2D& renderer);  // the overlay pass
};
