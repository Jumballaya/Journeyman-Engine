// Journeyman Editor: the window, the ImGui frame loop, and automation hooks.
//
// Automation (screenshots, smoke tests, steering from outside), all optional:
//   JM_EDITOR_PROJECT=<folder>      open this project at startup
//   JM_EDITOR_SCENE=<path>          and this scene in it
//   JM_EDITOR_SIZE=1600x1000        window size in points
//   JM_EDITOR_SCRIPT="30:play.toggle;90:@click 400 300"   steps at frames (see Automation.hpp)
//   JM_EDITOR_CONTROL=<folder>      a live session: steps appended to <folder>/in
//   JM_EDITOR_CAPTURE=<out.png> JM_EDITOR_FRAMES=<n>   save frame n and quit
//   JM_HEADLESS=1                   hidden window

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <nfd.hpp>

#include "Automation.hpp"
#include "Editor.hpp"
#include "LogBook.hpp"
#include "Project.hpp"
#include "Theme.hpp"
#include "core/logger/LoggerService.hpp"
#include "stb_image.h"

// editor/icon.png, compiled in (CMakeLists.txt).
extern const uint8_t editor_icon_data[];
extern const size_t editor_icon_size;

namespace {

std::string env(const char* name) {
  const char* v = std::getenv(name);
  return v ? v : "";
}

Editor* gEditor = nullptr;

// The window icon (Windows, Linux; macOS takes the app bundle's).
void setWindowIcon(GLFWwindow* window) {
  int w = 0, h = 0, channels = 0;
  stbi_uc* pixels = stbi_load_from_memory(editor_icon_data, static_cast<int>(editor_icon_size), &w, &h, &channels, 4);
  if (!pixels) return;
  GLFWimage image{w, h, pixels};
  glfwSetWindowIcon(window, 1, &image);
  stbi_image_free(pixels);
}

void onKey(GLFWwindow*, int key, int scancode, int action, int) {
  if (gEditor) gEditor->onKey(key, scancode, action);
}

// Files dropped from the OS land in the folder the Assets panel shows.
void onDrop(GLFWwindow*, int count, const char** paths) {
  if (!gEditor) return;
  std::vector<std::filesystem::path> files(paths, paths + count);
  gEditor->importFiles(files, gEditor->assetsFolder());
}

}  // namespace

int main(int, char**) {
  // Log to the user's settings folder; the console panel shows the same lines.
  std::filesystem::create_directories(settingsDir() / "logs");
  LoggerService::initialize(std::make_unique<Logger>("engine", (settingsDir() / "logs" / "editor.log").string()));
  captureEngineLog();

  // Inside an .app, GLFW would otherwise switch to Contents/Resources and break relative paths.
  glfwInitHint(GLFW_COCOA_CHDIR_RESOURCES, GLFW_FALSE);
  if (!glfwInit()) {
    std::fprintf(stderr, "Journeyman Editor: GLFW failed to start\n");
    return 1;
  }
  NFD::Guard nfdGuard;
  const bool headless = !env("JM_HEADLESS").empty() && env("JM_HEADLESS") != "0";
  int width = 1600, height = 1000;
  if (const std::string size = env("JM_EDITOR_SIZE"); !size.empty()) std::sscanf(size.c_str(), "%dx%d", &width, &height);

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
#ifdef __APPLE__
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
#endif
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_VISIBLE, headless ? GLFW_FALSE : GLFW_TRUE);
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
  GLFWwindow* window = glfwCreateWindow(width, height, "Journeyman Editor", nullptr, nullptr);
  if (!window) {
    std::fprintf(stderr, "Journeyman Editor: no OpenGL 4 window\n");
    glfwTerminate();
    return 1;
  }
#ifndef __APPLE__
  setWindowIcon(window);
#endif
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  gladLoadGL(glfwGetProcAddress);
  glfwSetKeyCallback(window, onKey);  // before ImGui, which chains to it
  glfwSetDropCallback(window, onDrop);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigWindowsMoveFromTitleBarOnly = true;
  io.ConfigDragClickToInputText = true;
  static const std::string iniPath = (settingsDir() / "layout.ini").string();
  io.IniFilename = iniPath.c_str();
  theme::apply();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 410");

  {
    Editor editor;
    gEditor = &editor;
    if (const std::string project = env("JM_EDITOR_PROJECT"); !project.empty()) editor.openProject(project);
    if (const std::string scene = env("JM_EDITOR_SCENE"); !scene.empty()) editor.openScene(scene);
    Automation automation(env("JM_EDITOR_SCRIPT"), env("JM_EDITOR_CONTROL"));
    const std::string capture = env("JM_EDITOR_CAPTURE");
    const int captureFrame = std::atoi(env("JM_EDITOR_FRAMES").c_str());

    double last = glfwGetTime();
    for (int frame = 0;; ++frame) {
      if (glfwWindowShouldClose(window)) {
        glfwSetWindowShouldClose(window, GLFW_FALSE);
        editor.requestQuit();
      }
      if (editor.quitConfirmed()) break;
      // Idle: wake on input, or ~20 times a second for tile animations and timers.
      if (automation.live()) {
        glfwPollEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(8));  // steered from outside: steady frames
      } else if (capture.empty() && !editor.busy()) {
        glfwWaitEventsTimeout(0.05);
      } else {
        glfwPollEvents();
      }
      // Idle politely when minimized.
      if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
      }
      const double now = glfwGetTime();
      const float dt = static_cast<float>(capture.empty() ? now - last : 1.0 / 60.0);
      last = now;
      automation.beforeFrame(editor, frame);

      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();
      editor.frame(dt);
      ImGui::Render();
      if (static std::string shownTitle; shownTitle != editor.windowTitle()) {
        shownTitle = editor.windowTitle();
        glfwSetWindowTitle(window, shownTitle.c_str());
      }

      int fbw = 0, fbh = 0;
      glfwGetFramebufferSize(window, &fbw, &fbh);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, fbw, fbh);
      const ImVec4 bg = theme::bg0;
      glClearColor(bg.x, bg.y, bg.z, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
      automation.afterRender(fbw, fbh);
      if (!capture.empty() && frame == captureFrame) {
        savePng(capture, fbw, fbh);
        editor.requestQuit();  // asset tabs save, as on a normal quit
        break;
      }
      glfwSwapBuffers(window);
    }
    gEditor = nullptr;
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
