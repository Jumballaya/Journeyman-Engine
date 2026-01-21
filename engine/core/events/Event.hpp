#pragma once
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <variant>

// Forward declarations for scene events
struct SceneHandle;

// Engine Events

namespace events {
struct WindowResized {
  int width{}, height{};
};
struct Quit {};

// Dynamic events to/from scripts
struct DynamicEvent {
  uint32_t id{};
  std::string name;
  nlohmann::json data;
};

// Scene events (forward declared, defined in SceneEvents.hpp)
struct SceneLoadStarted;
struct SceneLoadCompleted;
struct SceneUnloadStarted;
struct SceneUnloaded;
struct SceneActivated;
struct SceneDeactivated;
struct SceneTransitionStarted;
struct SceneTransitionCompleted;
}  // namespace events

// Include scene events
#include "../scene/SceneEvents.hpp"

using Event = std::variant<
    events::WindowResized, 
    events::Quit, 
    events::DynamicEvent,
    events::SceneLoadStarted,
    events::SceneLoadCompleted,
    events::SceneUnloadStarted,
    events::SceneUnloaded,
    events::SceneActivated,
    events::SceneDeactivated,
    events::SceneTransitionStarted,
    events::SceneTransitionCompleted
>;