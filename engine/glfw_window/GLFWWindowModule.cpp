#include "GLFWWindowModule.hpp"

#include <GLFW/glfw3.h>

#include <stdexcept>

#include "../core/app/ApplicationEvents.hpp"
#include "../core/app/Engine.hpp"
#include "../core/app/ModuleTags.hpp"
#include "../core/app/ModuleTraits.hpp"
#include "../core/app/Registration.hpp"
#include "../core/app/WindowEvents.hpp"
#include "../core/scripting/ScriptManager.hpp"

// GLFW owns the platform window and, after glfwInit, the OpenGL context that
// Renderer2D attaches to.
template <>
struct ModuleTraits<GLFWWindowModule> {
  using Provides = TypeList<WindowTag, OpenGLContextTag>;
  using DependsOn = TypeList<>;
};

REGISTER_MODULE(GLFWWindowModule)

namespace {

EventBus& busOf(GLFWwindow* window) {
  return static_cast<Engine*>(glfwGetWindowUserPointer(window))->getEventBus();
}

void onKey(GLFWwindow* window, int key, int scancode, int action, int) {
  EventBus& bus = busOf(window);
  if (action == GLFW_PRESS) bus.emit(EVT_KeyDown, events::KeyDown{scancode, key});
  if (action == GLFW_RELEASE) bus.emit(EVT_KeyUp, events::KeyUp{scancode, key});
  if (action == GLFW_REPEAT) bus.emit(EVT_KeyRepeat, events::KeyRepeat{scancode, key});
}

void onCursor(GLFWwindow* window, double x, double y) {
  // GLFW reports screen coordinates; the renderer works in framebuffer pixels (2x on HiDPI).
  int ww = 0, wh = 0, fw = 0, fh = 0;
  glfwGetWindowSize(window, &ww, &wh);
  glfwGetFramebufferSize(window, &fw, &fh);
  const float sx = ww > 0 ? static_cast<float>(fw) / static_cast<float>(ww) : 1.0f;
  const float sy = wh > 0 ? static_cast<float>(fh) / static_cast<float>(wh) : 1.0f;
  busOf(window).emit(EVT_MouseMove, events::MouseMove{static_cast<float>(x) * sx, static_cast<float>(y) * sy});
}

void onMouseButton(GLFWwindow* window, int button, int action, int) {
  if (action != GLFW_REPEAT) busOf(window).emit(EVT_MouseButton, events::MouseButton{button, action == GLFW_PRESS});
}

void onScroll(GLFWwindow* window, double dx, double dy) {
  busOf(window).emit(EVT_MouseWheel, events::MouseWheel{static_cast<float>(dx), static_cast<float>(dy)});
}

void onResize(GLFWwindow* window, int width, int height) {
  busOf(window).emit(EVT_WindowResize, events::WindowResized{width, height});
}

}  // namespace

void GLFWWindowModule::bindScriptApi(Engine& app) {
  ScriptManager& s = app.getScriptManager();
  s.bind("__jmWindowSetFullscreen", [this](bool on) {
    if (_window) setFullscreen(on);  // embedded (the editor): the host's window isn't the game's to change
  });
  s.bind("__jmWindowIsFullscreen", [this]() { return _fullscreen; });
  // No window of its own: the host's view (the editor), or none at all
  // (JM_RENDERER=none), which acts focused like a hidden headless window.
  s.bind("__jmWindowIsFocused", [this, &app]() { return _window ? _focused : !app.embedded() || app.viewFocused(); });
}

void GLFWWindowModule::initialize(Engine& app) {
  if (app.embedded()) return;
  if (app.getDevOptions().renderer == "none") {  // no window, no GL: nothing for GLFW to do
    JM_LOG_INFO("[GLFW Window] none (JM_RENDERER=none)");
    return;
  }

  // config.window: { width, height, resizable, vsync, fullscreen, hideCursor }
  const nlohmann::json& config = app.getManifest().config;
  const nlohmann::json win = config.contains("window") ? config["window"] : nlohmann::json::object();
  _headless = app.getDevOptions().headless;
  _vsync = win.value("vsync", true);

  if (!glfwInit()) throw std::runtime_error("GLFW init failed");
  // GL 4.1 core everywhere: macOS's ceiling, and all the engine uses, so any
  // driver from the last decade (or Mesa's software renderer) can run it.
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_RESIZABLE, win.value("resizable", true) ? GLFW_TRUE : GLFW_FALSE);
  glfwWindowHint(GLFW_VISIBLE, _headless ? GLFW_FALSE : GLFW_TRUE);
  // Headless frames are the window's size on every machine (no 2x Retina
  // framebuffer), so captures and goldens compare across platforms.
  glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, _headless ? GLFW_FALSE : GLFW_TRUE);
  _window = glfwCreateWindow(win.value("width", 1280), win.value("height", 720), app.getManifest().name.c_str(),
                             nullptr, nullptr);
  if (!_window) {
    glfwTerminate();
    throw std::runtime_error("GLFW window creation failed");
  }

  glfwMakeContextCurrent(_window);
  glfwSwapInterval(_vsync ? 1 : 0);
  glfwSetWindowUserPointer(_window, &app);
  glfwSetFramebufferSizeCallback(_window, onResize);
  glfwSetKeyCallback(_window, onKey);
  glfwSetCursorPosCallback(_window, onCursor);
  glfwSetMouseButtonCallback(_window, onMouseButton);
  glfwSetScrollCallback(_window, onScroll);
  if (win.value("hideCursor", false)) glfwSetInputMode(_window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
  if (const auto& pos = app.getDevOptions().windowPos) glfwSetWindowPos(_window, pos->first, pos->second);
  if (win.value("fullscreen", false)) setFullscreen(true);

  JM_LOG_INFO("[GLFW Window] initialized");
}

void GLFWWindowModule::setFullscreen(bool on) {
  if (on == _fullscreen) return;
  if (on) {
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
    if (!mode) return;
    glfwGetWindowPos(_window, &_windowedX, &_windowedY);
    glfwGetWindowSize(_window, &_windowedW, &_windowedH);
    glfwSetWindowMonitor(_window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
  } else {
    glfwSetWindowMonitor(_window, nullptr, _windowedX, _windowedY, _windowedW, _windowedH, 0);
  }
  _fullscreen = on;
  glfwSwapInterval(_vsync ? 1 : 0);  // a monitor change can reset the swap interval
}

void GLFWWindowModule::tickMainThread(Engine& app, float) {
  if (!_window) return;
  glfwPollEvents();
  glfwSwapBuffers(_window);
  // A hidden (headless) window never has focus, but its game should act focused.
  _focused = _headless || glfwGetWindowAttrib(_window, GLFW_FOCUSED) == GLFW_TRUE;
  if (glfwWindowShouldClose(_window)) app.getEventBus().emit(EVT_AppQuit, events::Quit{});
}

void GLFWWindowModule::shutdown(Engine&) {
  if (!_window) return;
  glfwDestroyWindow(_window);
  _window = nullptr;
  glfwTerminate();
  JM_LOG_INFO("[GLFW Window] shutdown");
}
