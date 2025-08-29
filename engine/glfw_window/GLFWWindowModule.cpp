#include "GLFWWindowModule.hpp"

#include "../core/app/ApplicationEvents.hpp"
#include "../core/app/Registration.hpp"
#include "WindowEvents.hpp"

REGISTER_MODULE(GLFWWindowModule)

void GLFWWindowModule::initialize(Application& app) {
  Window::Desc desc;
  auto& manifest = app.getManifest();
  desc.title = manifest.name;
  _window.initialize(desc);

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

void GLFWWindowModule::tickMainThread(Application& app, float /*dt*/) {
  _window.poll();
  _window.present();
  if (shouldClose()) {
    app.getEventBus().emit(EVT_AppQuit, events::Quit{});
    return;
  }
}

void GLFWWindowModule::shutdown(Application& app) {
  _window.destroy();
  glfwTerminate();
  JM_LOG_INFO("[GLFW Window] shutdown");
}