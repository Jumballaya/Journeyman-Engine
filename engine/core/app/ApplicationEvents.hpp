#pragma once

#include "../events/EventType.hpp"

inline constexpr EventType EVT_AppQuit = createEventType("application.quit");

namespace events {
struct Quit {};
}  // namespace events