#pragma once

#include <atomic>

#include "../core/app/Engine.hpp"
#include "../core/app/EngineModule.hpp"
#include "Window.hpp"

// The game window: created from config.window, emits key/resize/quit events,
// and lets scripts toggle fullscreen and check focus (Window in the runtime).
// Embedded engines have none: the host forwards input and sizes the view.
class GLFWWindowModule : public EngineModule {
 public:
  void initialize(Engine& app) override;
  void tickMainThread(Engine& app, float dt) override;
  void shutdown(Engine& app) override;
  const char* name() const override { return "GLFWWindowModule"; }

 private:
  Window _window;
  bool _headless = false;
  bool _embedded = false;  // hosted by an editor: no window here
  std::atomic<int> _fullscreenRequest{-1};  // -1 none, 0 windowed, 1 fullscreen
  std::atomic<bool> _fullscreen{false};
  std::atomic<bool> _focused{true};
};
