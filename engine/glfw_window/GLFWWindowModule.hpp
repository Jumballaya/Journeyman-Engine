#pragma once
#include "../core/app/Application.hpp"
#include "../core/app/EngineModule.hpp"
#include "../core/events/EventBus.hpp"
#include "Window.hpp"

class GLFWWindowModule : public EngineModule {
 public:
  GLFWWindowModule() = default;

  const char* name() const { return "GLFWWindowModule"; }

  void initialize(Application& app) {
    Window::Desc desc;
    auto& manifest = app.getManifest();
    desc.title = manifest.name;
    _window.initialize(desc);
  }

  void tickMainThread(Application& app, float /*dt*/) {
    _window.poll();
    _window.present();
    if (shouldClose()) {
      app.getEventBus().emit(events::Quit{});
    }
  }

  void shutdown(Application& app) {}

  bool shouldClose() const { return _window.shouldClose(); }
  Window& window() { return _window; }

 private:
  Window _window;
};
