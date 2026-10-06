#pragma once

#include <imgui.h>

// This frame's scroll and pinch, read as a canvas means them: trackpad
// scrolling pans, a mouse wheel zooms, a pinch zooms, Ctrl/Cmd+scroll zooms.
namespace scroll {

struct Gesture {
  ImVec2 pan{0, 0};   // screen points to move the view's camera by (x right, y up)
  float zoom = 1.0f;  // multiply the zoom by this (1 = none)
};

Gesture canvasGesture();

}  // namespace scroll
