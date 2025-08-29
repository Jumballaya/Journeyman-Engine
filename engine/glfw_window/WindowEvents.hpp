#pragma once

#include "../core/events/EventType.hpp"

inline constexpr EventType EVT_WindowResize = createEventType("window.resize");
inline constexpr EventType EVT_KeyUp = createEventType("window.keyup");
inline constexpr EventType EVT_KeyDown = createEventType("window.keydown");
inline constexpr EventType EVT_KeyRepeat = createEventType("window.keyrepeat");

namespace events {
struct WindowResized {
  int width{}, height{};
};

struct KeyDown {
  int scancode;
  int key;
};

struct KeyUp {
  int scancode;
  int key;
};

struct KeyRepeat {
  int scancode;
  int key;
};

}  // namespace events