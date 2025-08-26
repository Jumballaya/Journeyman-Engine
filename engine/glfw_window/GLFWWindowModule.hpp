#pragma once

#include <GLFW/glfw3.h>

#include "../core/app/Application.hpp"
#include "../core/app/EngineModule.hpp"
#include "../core/events/EventBus.hpp"
#include "../core/logger/logging.hpp"
#include "Window.hpp"

class GLFWWindowModule : public EngineModule {
 public:
  GLFWWindowModule() = default;
  ~GLFWWindowModule() = default;

  void initialize(Application& app);
  void tickMainThread(Application& app, float dt);
  void shutdown(Application& app);

  bool shouldClose() const { return _window.shouldClose(); }
  Window& window() { return _window; }

  const char* name() const override { return "GLFWWindowModule"; }

 private:
  Window _window;
};
