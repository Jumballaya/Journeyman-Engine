#include "Window.hpp"

#include <GLFW/glfw3.h>

#include <stdexcept>

Window::~Window() {}

void Window::initialize(const Desc& d) {
  if (!glfwInit()) throw std::runtime_error("GLFW init failed");

  _descriptor = d;

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, d.glMajor);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, d.glMinor);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
  glfwWindowHint(GLFW_RESIZABLE, d.resizable ? GLFW_TRUE : GLFW_FALSE);
  glfwWindowHint(GLFW_VISIBLE, d.visible ? GLFW_TRUE : GLFW_FALSE);

  _win = glfwCreateWindow(d.width, d.height, d.title.c_str(), nullptr, nullptr);
  if (!_win) {
    glfwTerminate();
    throw std::runtime_error("GLFW window creation failed");
  }

  glfwMakeContextCurrent(_win);
  setVSync(d.vsync);
  glfwSetWindowUserPointer(_win, this);
  glfwSetFramebufferSizeCallback(_win, handleResize);
  glfwSetKeyCallback(_win, handleKey);
  glfwSetCursorPosCallback(_win, handleCursor);
  glfwSetMouseButtonCallback(_win, handleMouseButton);
  glfwSetScrollCallback(_win, handleScroll);
  if (d.hideCursor) glfwSetInputMode(_win, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
  if (d.fullscreen) setFullscreen(true);
}

void Window::setFullscreen(bool on) {
  if (!_win || on == _fullscreen) return;
  if (on) {
    glfwGetWindowPos(_win, &_windowedX, &_windowedY);
    glfwGetWindowSize(_win, &_windowedW, &_windowedH);
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
    if (!mode) return;
    glfwSetWindowMonitor(_win, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
  } else {
    glfwSetWindowMonitor(_win, nullptr, _windowedX, _windowedY, _windowedW, _windowedH, 0);
  }
  _fullscreen = on;
  setVSync(_descriptor.vsync);
}

void Window::poll() { glfwPollEvents(); }
bool Window::isFocused() const { return _win && glfwGetWindowAttrib(_win, GLFW_FOCUSED) == GLFW_TRUE; }
void Window::present() { glfwSwapBuffers(_win); }
bool Window::shouldClose() const { return glfwWindowShouldClose(_win); }
void Window::setVSync(bool on) { glfwSwapInterval(on ? 1 : 0); }
void Window::setTitle(const std::string& t) { glfwSetWindowTitle(_win, t.c_str()); }

void Window::setResizeCallback(ResizeCallback callback) {
  _resizeCallback = std::move(callback);
}

void Window::setKeyCallback(KeyCallback callback) {
  _keyCallback = std::move(callback);
}

void Window::setMouseCallbacks(MouseMoveCallback move, MouseButtonCallback button, ScrollCallback scroll) {
  _mouseMoveCallback = std::move(move);
  _mouseButtonCallback = std::move(button);
  _scrollCallback = std::move(scroll);
}

void Window::handleCursor(GLFWwindow* win, double x, double y) {
  auto* self = static_cast<Window*>(glfwGetWindowUserPointer(win));
  if (!self || !self->_mouseMoveCallback) return;
  // GLFW reports screen coordinates; the renderer works in framebuffer pixels (2x on HiDPI).
  int ww = 0, wh = 0, fw = 0, fh = 0;
  glfwGetWindowSize(win, &ww, &wh);
  glfwGetFramebufferSize(win, &fw, &fh);
  const float sx = ww > 0 ? static_cast<float>(fw) / static_cast<float>(ww) : 1.0f;
  const float sy = wh > 0 ? static_cast<float>(fh) / static_cast<float>(wh) : 1.0f;
  self->_mouseMoveCallback(static_cast<float>(x) * sx, static_cast<float>(y) * sy);
}

void Window::handleMouseButton(GLFWwindow* win, int button, int action, int) {
  auto* self = static_cast<Window*>(glfwGetWindowUserPointer(win));
  if (self && self->_mouseButtonCallback && action != GLFW_REPEAT) self->_mouseButtonCallback(button, action == GLFW_PRESS);
}

void Window::handleScroll(GLFWwindow* win, double dx, double dy) {
  auto* self = static_cast<Window*>(glfwGetWindowUserPointer(win));
  if (self && self->_scrollCallback) self->_scrollCallback(static_cast<float>(dx), static_cast<float>(dy));
}

void Window::handleResize(GLFWwindow* win, int width, int height) {
  auto* self = static_cast<Window*>(glfwGetWindowUserPointer(win));
  if (self) {
    self->_width = width;
    self->_height = height;
    if (self->_resizeCallback) {
      self->_resizeCallback(width, height);
    }
  }
}

void Window::handleKey(GLFWwindow* win, int key, int scancode, int action, int mods) {
  auto* self = static_cast<Window*>(glfwGetWindowUserPointer(win));
  if (self) {
    if (self->_keyCallback) {
      self->_keyCallback(key, scancode, action, mods);
    }
  }
}

void Window::destroy() {
  if (_win) {
    glfwDestroyWindow(_win);
    _win = nullptr;
    _width = 0;
    _height = 0;
  }
}