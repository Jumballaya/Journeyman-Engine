#include "GLFWWindowModule.hpp"

#include <GLFW/glfw3.h>

#include "../core/app/ApplicationEvents.hpp"
#include "../core/app/ModuleTags.hpp"
#include "../core/app/ModuleTraits.hpp"
#include "../core/app/Registration.hpp"
#include "../core/scripting/ScriptManager.hpp"
#include "WindowEvents.hpp"

// GLFW owns the platform window and, after glfwInit, the OpenGL context that
// Renderer2D attaches to.
template <>
struct ModuleTraits<GLFWWindowModule> {
  using Provides = TypeList<WindowTag, OpenGLContextTag>;
  using DependsOn = TypeList<>;
};

REGISTER_MODULE(GLFWWindowModule)

void GLFWWindowModule::initialize(Engine& app) {
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

  // Scripts run on worker threads and GLFW is main-thread only: requests are
  // stored here and applied in tickMainThread; state is cached there too.
  ScriptManager& s = app.getScriptManager();
  s.bind("__jmWindowSetFullscreen", [this](bool on) { _fullscreenRequest = on ? 1 : 0; });
  s.bind("__jmWindowIsFullscreen", [this]() { return _fullscreen.load(); });
  s.bind("__jmWindowIsFocused", [this]() { return _focused.load(); });

  JM_LOG_INFO("[GLFW Window] initialized");
}

void GLFWWindowModule::tickMainThread(Engine& app, float) {
  if (int request = _fullscreenRequest.exchange(-1); request >= 0) _window.setFullscreen(request == 1);
  _window.poll();
  _window.present();
  _fullscreen = _window.isFullscreen();
  _focused = _headless || _window.isFocused();  // a hidden window never has focus
  if (_window.shouldClose()) app.getEventBus().emit(EVT_AppQuit, events::Quit{});
}

void GLFWWindowModule::shutdown(Engine&) {
  _window.destroy();
  glfwTerminate();
  JM_LOG_INFO("[GLFW Window] shutdown");
}
