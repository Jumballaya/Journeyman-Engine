#include "GLFWWindowModule.hpp"

#include <GLFW/glfw3.h>

#include "../core/app/ApplicationEvents.hpp"
#include "../core/app/ModuleTags.hpp"
#include "../core/app/ModuleTraits.hpp"
#include "../core/app/Registration.hpp"
#include "../core/scripting/ScriptManager.hpp"
#include "../core/app/WindowEvents.hpp"

// GLFW owns the platform window and, after glfwInit, the OpenGL context that
// Renderer2D attaches to.
template <>
struct ModuleTraits<GLFWWindowModule> {
  using Provides = TypeList<WindowTag, OpenGLContextTag>;
  using DependsOn = TypeList<>;
};

REGISTER_MODULE(GLFWWindowModule)

void GLFWWindowModule::initialize(Engine& app) {
  ScriptManager& s = app.getScriptManager();
  if (app.embedded()) {
    // The host owns the window and context; scripts see its focus, never fullscreen.
    _embedded = true;
    s.bind("__jmWindowSetFullscreen", [](bool) {});
    s.bind("__jmWindowIsFullscreen", []() { return false; });
    s.bind("__jmWindowIsFocused", [&app]() { return app.viewFocused(); });
    return;
  }

  // config.window: { width, height, resizable, vsync, fullscreen, hideCursor }
  Window::Desc desc;
  desc.title = app.getManifest().name;
  const auto& config = app.getManifest().config;
  if (config.contains("window")) {
    const auto& win = config["window"];
    desc.width = win.value("width", desc.width);
    desc.height = win.value("height", desc.height);
    desc.resizable = win.value("resizable", desc.resizable);
    desc.vsync = win.value("vsync", desc.vsync);
    desc.fullscreen = win.value("fullscreen", desc.fullscreen);
    desc.hideCursor = win.value("hideCursor", desc.hideCursor);
  }
  _headless = app.getDevOptions().headless;
  desc.visible = !_headless;
  _window.initialize(desc);

  _window.setResizeCallback([&app](int w, int h) {
    app.getEventBus().emit(EVT_WindowResize, events::WindowResized{w, h});
  });
  _window.setKeyCallback([&app](int key, int scancode, int action, int) {
    if (action == GLFW_PRESS) app.getEventBus().emit(EVT_KeyDown, events::KeyDown{scancode, key});
    if (action == GLFW_RELEASE) app.getEventBus().emit(EVT_KeyUp, events::KeyUp{scancode, key});
    if (action == GLFW_REPEAT) app.getEventBus().emit(EVT_KeyRepeat, events::KeyRepeat{scancode, key});
  });
  _window.setMouseCallbacks(
      [&app](float x, float y) { app.getEventBus().emit(EVT_MouseMove, events::MouseMove{x, y}); },
      [&app](int button, bool down) { app.getEventBus().emit(EVT_MouseButton, events::MouseButton{button, down}); },
      [&app](float dx, float dy) { app.getEventBus().emit(EVT_MouseWheel, events::MouseWheel{dx, dy}); });

  // Scripts run on worker threads and GLFW is main-thread only: requests are
  // stored here and applied in tickMainThread; state is cached there too.
  s.bind("__jmWindowSetFullscreen", [this](bool on) { _fullscreenRequest = on ? 1 : 0; });
  s.bind("__jmWindowIsFullscreen", [this]() { return _fullscreen.load(); });
  s.bind("__jmWindowIsFocused", [this]() { return _focused.load(); });

  JM_LOG_INFO("[GLFW Window] initialized");
}

void GLFWWindowModule::tickMainThread(Engine& app, float) {
  if (_embedded) return;
  if (int request = _fullscreenRequest.exchange(-1); request >= 0) _window.setFullscreen(request == 1);
  _window.poll();
  _window.present();
  _fullscreen = _window.isFullscreen();
  _focused = _headless || _window.isFocused();  // a hidden window never has focus
  if (_window.shouldClose()) app.getEventBus().emit(EVT_AppQuit, events::Quit{});
}

void GLFWWindowModule::shutdown(Engine&) {
  if (_embedded) return;
  _window.destroy();
  glfwTerminate();
  JM_LOG_INFO("[GLFW Window] shutdown");
}
