#pragma once

#include <functional>

#include <glm/glm.hpp>

#include "../core/ecs/World.hpp"
#include "../core/ecs/system/System.hpp"
#include "CameraComponent.hpp"

// The game's view: where it looks and how close. Camera entities follow their
// targets; the highest priority one is shown, eased into over its `blend`.
// With none, scripts place the view (Camera.setPosition).
class Cameras {
 public:
  struct View {
    glm::vec2 center{0.0f};
    float zoom = 1.0f;
  };

  // One frame: cameras follow their targets, and the shown one is picked.
  // `gameSize`: the game's logical size in px (what bounds keep inside).
  void update(World& world, float dt, glm::vec2 gameSize);
  const View& view() const { return _view; }
  EntityId shown() const { return _shown; }

  // Scripts' Camera: moves the shown camera there at once, or zooms it; with none, the view.
  void setPosition(World& world, glm::vec2 center);
  void setZoom(World& world, float zoom);
  // A camera entity follows `target` (kNoEntityId: stops).
  static void follow(World& world, EntityId camera, EntityId target);
  void reset() { *this = Cameras{}; }  // a scene unloaded

 private:
  EntityId pick(World& world) const;  // the camera entity to show: the highest priority
  View _view;
  View _free;  // the view while no camera entity is shown
  EntityId _shown = kNoEntityId;
  View _from;  // blending: the view when the shown camera changed
  float _blendTime = 0.0f, _blendDuration = 0.0f;
};

// Runs Cameras::update after physics, so cameras see where things ended up.
class CameraSystem : public System {
 public:
  using GameSize = std::function<glm::vec2()>;
  CameraSystem(Cameras& cameras, GameSize gameSize) : _cameras(cameras), _gameSize(std::move(gameSize)) {}
  void update(World& world, float dt) override { _cameras.update(world, dt, _gameSize()); }
  const char* name() const override { return "CameraSystem"; }

 private:
  Cameras& _cameras;
  GameSize _gameSize;
};
