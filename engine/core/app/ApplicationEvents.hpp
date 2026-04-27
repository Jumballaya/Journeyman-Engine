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

// Fires AFTER the new scene's entities have been loaded successfully, not at
// the moment transitionTo is called. That keeps Started/Finished symmetric:
// if the load throws, neither Started nor Finished fires — only
// SceneLoadFailed. Subscribers that planned UI animations against duration
// can safely start them on Started without worrying about an asymmetric pair.
struct SceneTransitionStarted {
  AssetHandle fromScene;
  AssetHandle toScene;
  float duration;
};

struct SceneTransitionFinished {
  AssetHandle fromScene;
  AssetHandle toScene;
};

// Fires when SceneManager attempts to load a scene and the loader throws
// (malformed JSON, bad component data, missing prefab reference, etc.). The
// world is left in a clean state with no current scene; callers can react by
// loading a fallback. The original exception is re-thrown to the caller.
struct SceneLoadFailed {
  AssetHandle attempted;
};
}  // namespace events
