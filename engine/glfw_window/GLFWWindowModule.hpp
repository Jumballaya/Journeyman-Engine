#pragma once

#include <GLFW/glfw3.h>

#include <atomic>

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

  // Scripts call these from worker threads; GLFW calls must happen on the
  // main thread, so the request is applied in tickMainThread.
  void requestFullscreen(bool on) { _fullscreenRequest = on ? 1 : 0; }
  bool isFullscreen() const { return _window.isFullscreen(); }
  // Cached once per frame on the main thread (safe to read from scripts).
  bool isFocused() const { return _focused.load(std::memory_order_relaxed); }

  const char* name() const override { return "GLFWWindowModule"; }

 private:
  Window _window;
  std::atomic<int> _fullscreenRequest{-1};
  std::atomic<bool> _focused{true};
};
