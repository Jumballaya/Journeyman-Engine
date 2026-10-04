#include "GLFWWindowModule.hpp"

#include "../core/app/ApplicationEvents.hpp"
#include "../core/app/ModuleTags.hpp"
#include "../core/app/ModuleTraits.hpp"
#include "../core/app/Registration.hpp"
#include "WindowEvents.hpp"

#include <cstdlib>
#include <wasm3.h>

#include "../core/scripting/HostFunction.hpp"
#include "../core/scripting/ScriptManager.hpp"

namespace {
GLFWWindowModule* s_window = nullptr;

void setWindowHostContext(GLFWWindowModule* window) { s_window = window; }
void setWindowHostContext(GLFWWindowModule& window) { s_window = &window; }

m3ApiRawFunction(jmWindowSetFullscreen) {
  m3ApiGetArg(int32_t, on);
  if (s_window) s_window->requestFullscreen(on != 0);
  m3ApiSuccess();
}

m3ApiRawFunction(jmWindowIsFullscreen) {
  m3ApiReturnType(int32_t);
  m3ApiReturn(s_window && s_window->isFullscreen() ? 1 : 0);
}

m3ApiRawFunction(jmWindowIsFocused) {
  m3ApiReturnType(int32_t);
  m3ApiReturn(!s_window || s_window->isFocused() ? 1 : 0);
}

void registerWindowHostFunctions(ScriptManager& scripts) {
  scripts.registerHostFunction("__jmWindowIsFocused", {"env", "__jmWindowIsFocused", "i()", &jmWindowIsFocused});
  scripts.registerHostFunction("__jmWindowSetFullscreen", {"env", "__jmWindowSetFullscreen", "v(i)", &jmWindowSetFullscreen});
  scripts.registerHostFunction("__jmWindowIsFullscreen", {"env", "__jmWindowIsFullscreen", "i()", &jmWindowIsFullscreen});
}
}  // namespace

// GLFW owns the platform window and, after glfwInit, the OpenGL context that
// Renderer2D attaches to.
template <>
struct ModuleTraits<GLFWWindowModule> {
  using Provides = TypeList<WindowTag, OpenGLContextTag>;
  using DependsOn = TypeList<>;
};

REGISTER_MODULE(GLFWWindowModule)

void GLFWWindowModule::initialize(Engine& app) {
  Window::Desc desc;
  const auto& manifest = app.getManifest();
  desc.title = manifest.name;
  // config.window: { width, height, resizable, vsync, fullscreen, hideCursor }
  if (manifest.config.contains("window")) {
    const auto& win = manifest.config["window"];
    desc.width = win.value("width", desc.width);
    desc.height = win.value("height", desc.height);
    desc.resizable = win.value("resizable", desc.resizable);
    desc.vsync = win.value("vsync", desc.vsync);
    desc.fullscreen = win.value("fullscreen", desc.fullscreen);
    desc.hideCursor = win.value("hideCursor", desc.hideCursor);
  }
  // JM_HEADLESS=1: render offscreen (no visible window) for automated runs.
  if (const char* headless = std::getenv("JM_HEADLESS"); headless && *headless && *headless != '0') {
    desc.visible = false;
  }
  _window.initialize(desc);
  setWindowHostContext(*this);
  registerWindowHostFunctions(app.getScriptManager());

  _window.setResizeCallback([&app](int w, int h) {
    events::WindowResized evt{w, h};
    app.getEventBus().emit<events::WindowResized>(EVT_WindowResize, evt);
  });

  _window.setKeyCallback([&app](int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS) {
      events::KeyDown evt{scancode, key};
      app.getEventBus().emit<events::KeyDown>(EVT_KeyDown, evt);
    }

    if (action == GLFW_RELEASE) {
      events::KeyUp evt{scancode, key};
      app.getEventBus().emit<events::KeyUp>(EVT_KeyUp, evt);
    }

    if (action == GLFW_REPEAT) {
      events::KeyRepeat evt{scancode, key};
      app.getEventBus().emit<events::KeyRepeat>(EVT_KeyRepeat, evt);
    }
  });

  JM_LOG_INFO("[GLFW Window] initialized");
}

void GLFWWindowModule::tickMainThread(Engine& app, float /*dt*/) {
  if (int req = _fullscreenRequest.exchange(-1); req >= 0) _window.setFullscreen(req == 1);
  _window.poll();
  // A hidden (headless) window never has focus; report it as focused so
  // automated runs don't auto-pause.
  _focused.store(_window.isFocused() || std::getenv("JM_HEADLESS") != nullptr, std::memory_order_relaxed);
  _window.present();
  if (shouldClose()) {
    app.getEventBus().emit(EVT_AppQuit, events::Quit{});
    return;
  }
}

void GLFWWindowModule::shutdown(Engine& app) {
  setWindowHostContext(nullptr);
  _window.destroy();
  glfwTerminate();
  JM_LOG_INFO("[GLFW Window] shutdown");
}