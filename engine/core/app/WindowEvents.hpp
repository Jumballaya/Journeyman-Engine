#pragma once

#include "../events/EventType.hpp"

inline constexpr EventType EVT_WindowResize = createEventType("window.resize");
inline constexpr EventType EVT_KeyUp = createEventType("window.keyup");
inline constexpr EventType EVT_KeyDown = createEventType("window.keydown");
inline constexpr EventType EVT_KeyRepeat = createEventType("window.keyrepeat");
inline constexpr EventType EVT_MouseMove = createEventType("window.mousemove");
inline constexpr EventType EVT_MouseButton = createEventType("window.mousebutton");
inline constexpr EventType EVT_MouseWheel = createEventType("window.mousewheel");
inline constexpr EventType EVT_NamedKey = createEventType("window.namedkey");

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

// The pointer, in framebuffer pixels from the top-left (what the renderer draws into).
struct MouseMove {
  float x, y;
};

// 0 = left, 1 = right, 2 = middle.
struct MouseButton {
  int button;
  bool down;
};

// Scroll this event: y up (away from the user), x right; trackpads give fractions.
struct MouseWheel {
  float dx, dy;
};

// A key by its name (as play sessions keep keys): a replayed play's.
struct NamedKey {
  char name[24];
  bool down;
};

}  // namespace events