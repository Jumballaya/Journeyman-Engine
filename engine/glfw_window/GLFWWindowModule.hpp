#pragma once


#include "../core/app/EngineModule.hpp"

struct GLFWwindow;

// The game window: created from config.window, emits key/mouse/resize/quit events,
// and lets scripts toggle fullscreen and check focus (Window in the runtime).
// Embedded engines have none: the host forwards input, sizes the view and reports focus.
class GLFWWindowModule : public EngineModule {
 public:
  void initialize(Engine& app) override;
  void tickMainThread(Engine& app, float dt) override;
  void shutdown(Engine& app) override;
  const char* name() const override { return "GLFWWindowModule"; }

 private:
  // Borderless on the primary monitor, or back to the last windowed placement.
  void setFullscreen(bool on);

  GLFWwindow* _window = nullptr;  // null when embedded
  bool _headless = false;
  bool _vsync = true;
  bool _fullscreen = false;
  bool _focused = true;
  int _windowedX = 100, _windowedY = 100, _windowedW = 0, _windowedH = 0;
};
