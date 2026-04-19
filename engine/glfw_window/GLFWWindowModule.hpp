#pragma once

#include <GLFW/glfw3.h>

#include "../core/app/Engine.hpp"
#include "../core/app/EngineModule.hpp"
#include "../core/events/EventBus.hpp"
#include "../core/logger/logging.hpp"
#include "Window.hpp"

class GLFWWindowModule : public EngineModule {
 public:
  GLFWWindowModule() = default;
  ~GLFWWindowModule() = default;

  void initialize(Engine& app) override;
  void tickMainThread(Engine& app, float dt) override;
  void shutdown(Engine& app) override;

  bool shouldClose() const { return _window.shouldClose(); }
  Window& window() { return _window; }

  const char* name() const override { return "GLFWWindowModule"; }

 private:
  Window _window;
};
