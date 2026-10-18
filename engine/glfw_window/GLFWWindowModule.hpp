#pragma once

#include <string>


#include "../core/app/EngineModule.hpp"

struct GLFWwindow;

// The game window: created from config.window, emits key/mouse/resize/quit events,
// and lets scripts toggle fullscreen and check focus (Window in the runtime).
// Embedded engines have none: the host forwards input, sizes the view and reports focus.
class GLFWWindowModule : public EngineModule {
 public:
  void bindScriptApi(Engine& app) override;
  void initialize(Engine& app) override;
  void draw(Engine& app, float dt) override;
  void shutdown(Engine& app) override;
  const char* name() const override { return "GLFWWindowModule"; }

 private:
  // Borderless on the monitor the window is mostly on, or back to the last
  // windowed placement. An automated run only notes it: its frames stay the
  // window's size on every machine, and nobody's screen is taken over.
  void setFullscreen(bool on);

  GLFWwindow* _window = nullptr;  // null when embedded
  bool _headless = false;
  bool _vsync = true;
  bool _fullscreen = false;
  bool _automated = false;
  bool _forced = false;  // JM_FULLSCREEN chose it at launch
  std::string _shownNotice;
  bool _devicesWereMuted = false;
  int _windowedX = 100, _windowedY = 100, _windowedW = 0, _windowedH = 0;
};
