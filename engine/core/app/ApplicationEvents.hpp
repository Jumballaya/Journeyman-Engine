#pragma once

#include "../assets/AssetHandle.hpp"
#include "../events/EventType.hpp"

inline constexpr EventType EVT_AppQuit = createEventType("application.quit");

inline constexpr EventType EVT_SceneUnloading = createEventType("scene.unloading");
inline constexpr EventType EVT_SceneLoaded = createEventType("scene.loaded");
inline constexpr EventType EVT_SceneTransitionStarted = createEventType("scene.transition_started");
inline constexpr EventType EVT_SceneTransitionFinished = createEventType("scene.transition_finished");

namespace events {
struct Quit {};

struct SceneUnloading {
  AssetHandle scene;
};

struct SceneLoaded {
  AssetHandle scene;
};

struct SceneTransitionStarted {
  AssetHandle fromScene;
  AssetHandle toScene;
  float duration;
};

struct SceneTransitionFinished {
  AssetHandle fromScene;
  AssetHandle toScene;
};
}  // namespace events
