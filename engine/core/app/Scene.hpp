#pragma once

#include <cstdint>

using SceneHandle = uint32_t;

enum SceneState {
    Playing,
    Paused,
};
struct Scene {
    Scene() = default;
    ~Scene() = default;

    SceneState state = SceneState::Paused;
};
