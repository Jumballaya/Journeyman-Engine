#pragma once

#include <array>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "../core/app/EngineModule.hpp"
#include "../core/ecs/entity/EntityId.hpp"
#include "../ui/DebugDraw.hpp"

class AgentRun;
class Renderer2D;
class Renderer2DModule;
class UIModule;

// F8's moment on screen (AgentRun::hold; agent build only: journeyman_engine
// links this, the player doesn't). While the run is held: a click picks the
// entity under the pointer, shift-click adds or takes it away; the picked are
// outlined and named over the frame, under a "Paused · F8" badge; Enter
// saves the moment, F8 or Esc goes on without, `.` steps a frame. The game's
// devices are muted then: this reads the window's itself.
class MomentsModule : public EngineModule {
 public:
  void initialize(Engine& app) override;
  void shutdown(Engine&) override {}
  // The window's keys and mouse, while held.
  void draw(Engine& app, float dt) override;
  // The driver's `pick x y [add]`: as a click (add: shift-click), x y in logical px.
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

  // Where an entity is drawn in the world, as a quad (counter-clockwise): its
  // sprite, else its collider's box; null if neither.
  std::optional<std::array<glm::vec2, 4>> quadOf(EntityId id) const;
  // Its box in the frame's picture: AgentRun::setWhereDrawn.
  nlohmann::json rectOf(EntityId id) const;
  void paint(Renderer2D& renderer);  // the overlay pass
};
