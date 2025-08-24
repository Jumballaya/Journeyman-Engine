#include "Window.hpp"

#include <GLFW/glfw3.h>

#include <stdexcept>

namespace {
int g_glfwRef = 0;
void ensureInit() {
  if (g_glfwRef++ == 0) {
    if (!glfwInit()) throw std::runtime_error("GLFW init failed");
  }
}
void ensureTerm() {
  if (--g_glfwRef == 0) glfwTerminate();
}
}  // namespace

Window::~Window() {
  if (_win) {
    glfwDestroyWindow(_win);
    _win = nullptr;
    _width = 0;
    _height = 0;
    ensureTerm();
  }
}

void Window::initialize(const Desc& d) {
  ensureInit();

  _descriptor = d;

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, d.glMajor);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, d.glMinor);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
  glfwWindowHint(GLFW_RESIZABLE, d.resizable ? GLFW_TRUE : GLFW_FALSE);

  _win = glfwCreateWindow(d.width, d.height, d.title.c_str(), nullptr, nullptr);
  if (!_win) {
    ensureTerm();
    throw std::runtime_error("GLFW window creation failed");
  }

  glfwMakeContextCurrent(_win);
  setVSync(d.vsync);
  glfwSetWindowUserPointer(_win, this);
  glfwSetFramebufferSizeCallback(_win, handleResize);
}

void Window::poll() { glfwPollEvents(); }
void Window::present() { glfwSwapBuffers(_win); }
bool Window::shouldClose() const { return glfwWindowShouldClose(_win); }
void Window::setVSync(bool on) { glfwSwapInterval(on ? 1 : 0); }
void Window::setTitle(const std::string& t) { glfwSetWindowTitle(_win, t.c_str()); }

void Window::setResizeCallback(ResizeCallback callback) {
  _resizeCallback = std::move(callback);
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