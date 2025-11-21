#pragma once

#include <cstdint>

#include "../ecs/World.hpp"

using SceneHandle = uint32_t;

enum SceneState {
  Playing,
  Paused,
};
struct Scene {
  Scene() = default;
  ~Scene() = default;

  SceneState state = SceneState::Paused;

 private:
  World _world;
};
