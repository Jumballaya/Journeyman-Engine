// macOS: AppKit knows what GLFW drops: whether a scroll came from a trackpad
// (precise deltas) or a wheel, and pinch gestures. A local event monitor
// watches both and passes every event on untouched.
#import <AppKit/AppKit.h>

#include <imgui.h>

namespace scroll {

namespace {
bool sPrecise = false;
float sPinch = 0.0f;
int sPinchFrame = -1;  // the ImGui frame running when the pinch arrived

void watch() {
  static bool installed = false;
  if (installed) return;
  installed = true;
  [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel | NSEventMaskMagnify
                                        handler:^NSEvent*(NSEvent* event) {
                                          if (event.type == NSEventTypeScrollWheel) {
                                            sPrecise = event.hasPreciseScrollingDeltas;
                                          } else {
                                            const int frame = ImGui::GetFrameCount();
                                            if (frame != sPinchFrame) sPinch = 0.0f, sPinchFrame = frame;
                                            sPinch += static_cast<float>(event.magnification);
                                          }
                                          return event;
                                        }];
}
}  // namespace

bool fromPreciseDevice() {
  watch();
  return sPrecise;
}

// Events are polled between frames, so a pinch stamped with the last frame belongs to this one.
float pinchThisFrame() {
  watch();
  return sPinchFrame == ImGui::GetFrameCount() - 1 ? sPinch : 0.0f;
}

}  // namespace scroll
