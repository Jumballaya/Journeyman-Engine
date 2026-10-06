#include "Scroll.hpp"

#include <algorithm>
#include <cmath>

namespace scroll {

#ifdef __APPLE__
// Scroll.mm: what the last scroll event came from, and this frame's pinch.
bool fromPreciseDevice();
float pinchThisFrame();
#else
// No device info here: whole wheel notches are a mouse, anything finer a touchpad.
static bool fromPreciseDevice() {
  const ImGuiIO& io = ImGui::GetIO();
  return io.MouseWheelH != 0.0f || std::abs(io.MouseWheel - std::round(io.MouseWheel)) > 0.001f;
}
static float pinchThisFrame() { return 0.0f; }
#endif

Gesture canvasGesture() {
  const ImGuiIO& io = ImGui::GetIO();
  Gesture g;
  const bool scrolled = io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f;
  if (scrolled && fromPreciseDevice() && !io.KeyCtrl) {
    // GLFW scales precise deltas by 0.1: undo it so content follows the fingers.
    g.pan = ImVec2(-io.MouseWheelH * 10.0f, io.MouseWheel * 10.0f);
  } else if (io.MouseWheel != 0.0f) {
    // Accelerated wheels can report big jumps: cap a frame's zoom at 3 notches.
    g.zoom = std::pow(1.18f, std::clamp(io.MouseWheel, -3.0f, 3.0f));
  }
  g.zoom *= 1.0f + pinchThisFrame();
  return g;
}

}  // namespace scroll
