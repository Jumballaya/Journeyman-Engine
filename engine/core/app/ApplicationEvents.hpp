#pragma once

#include "../assets/AssetHandle.hpp"
#include "../events/EventType.hpp"

inline constexpr EventType EVT_AppQuit = createEventType("application.quit");

inline constexpr EventType EVT_SceneUnloading = createEventType("scene.unloading");
inline constexpr EventType EVT_SceneLoaded = createEventType("scene.loaded");
inline constexpr EventType EVT_SceneTransitionStarted = createEventType("scene.transition_started");
inline constexpr EventType EVT_SceneTransitionFinished = createEventType("scene.transition_finished");
inline constexpr EventType EVT_SceneLoadFailed = createEventType("scene.load_failed");

namespace events {
struct Quit {};

struct SceneUnloading {
  AssetHandle scene;
};

struct SceneLoaded {
  AssetHandle scene;
};

// Fires after the new scene loaded; a failed load fires only SceneLoadFailed, so
// Started and Finished always come in pairs.
struct SceneTransitionStarted {
  AssetHandle fromScene;
  AssetHandle toScene;
  float duration;
};

struct SceneTransitionFinished {
  AssetHandle fromScene;
  AssetHandle toScene;
};

// A scene failed to load; no scene is current and the exception is rethrown to
// the caller.
struct SceneLoadFailed {
  AssetHandle attempted;
};
}  // namespace events
