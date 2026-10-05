// Journeyman Editor: the window, the ImGui frame loop, and automation hooks.
//
// Automation (for screenshots and smoke tests), all optional:
//   JM_EDITOR_PROJECT=<folder>      open this project at startup
//   JM_EDITOR_SCENE=<path>          and this scene in it
//   JM_EDITOR_SIZE=1600x1000        window size in points
//   JM_EDITOR_SCRIPT="30:play.toggle;90:view.panel.Console"   run commands at frames;
//     "@mouse x y", "@down", "@up", "@rdown", "@rup", "@wheel dy", "@key W", "@ctrl", "@shift"
//     (hold until "@release"), "@select Name" simulate input (points from the window's top-left)
//   JM_EDITOR_CAPTURE=<out.png> JM_EDITOR_FRAMES=<n>   save frame n and quit
//   JM_HEADLESS=1                   hidden window

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <thread>

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <nfd.hpp>

#include "Editor.hpp"
#include "LogBook.hpp"
#include "Project.hpp"
#include "Theme.hpp"
#include "core/logger/LoggerService.hpp"
#include "stb_image_write.h"

namespace {

std::string env(const char* name) {
  const char* v = std::getenv(name);
  return v ? v : "";
}

Editor* gEditor = nullptr;

void onKey(GLFWwindow*, int key, int scancode, int action, int) {
  if (gEditor) gEditor->onKey(key, scancode, action);
}

void savePng(const std::string& path, int width, int height) {
  std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadBuffer(GL_BACK);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  const size_t row = static_cast<size_t>(width) * 4;
  for (int y = 0; y < height / 2; ++y) {  // GL rows are bottom-up
    std::swap_ranges(pixels.begin() + y * row, pixels.begin() + (y + 1) * row, pixels.begin() + (height - 1 - y) * row);
  }
  stbi_write_png(path.c_str(), width, height, 4, pixels.data(), static_cast<int>(row));
}

// Simulated input for automation: "@mouse 400 300", "@down", "@key W", "@select Player".
void simulate(Editor& editor, const std::string& action) {
  ImGuiIO& io = ImGui::GetIO();
  std::istringstream in(action);
  std::string verb;
  in >> verb;
  if (verb == "@mouse") {
    float x = 0, y = 0;
    in >> x >> y;
    io.AddMousePosEvent(x, y);
  } else if (verb == "@down" || verb == "@up") {
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, verb == "@down");
  } else if (verb == "@rdown" || verb == "@rup") {
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, verb == "@rdown");
  } else if (verb == "@wheel") {
    float dy = 0;
    in >> dy;
    io.AddMouseWheelEvent(0, dy);
  } else if (verb == "@ctrl" || verb == "@shift" || verb == "@release") {
    const bool down = verb != "@release";
    if (verb == "@ctrl" || !down) io.AddKeyEvent(ImGuiMod_Ctrl, down && verb == "@ctrl");
    if (verb == "@shift" || !down) io.AddKeyEvent(ImGuiMod_Shift, down && verb == "@shift");
  } else if (verb == "@key") {
    std::string name;
    in >> name;
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
      if (name == ImGui::GetKeyName(static_cast<ImGuiKey>(k))) {
        io.AddKeyEvent(static_cast<ImGuiKey>(k), true);
        io.AddKeyEvent(static_cast<ImGuiKey>(k), false);
      }
    }
  } else if (verb == "@select") {
    std::string name;
    std::getline(in >> std::ws, name);
    if (SceneDocument* scene = editor.scene()) {
      for (size_t i = 0; i < scene->size(); ++i) {
        if (scene->displayName(i) == name) editor.select(scene->uid(i));
      }
    }
  }
}

// "30:play.toggle;90:view.panel.Console" → {30: [play.toggle], 90: [...]}
std::multimap<int, std::string> parseScript(const std::string& text) {
  std::multimap<int, std::string> out;
  std::stringstream in(text);
  for (std::string item; std::getline(in, item, ';');) {
    const size_t colon = item.find(':');
    if (colon == std::string::npos) continue;
    out.emplace(std::atoi(item.substr(0, colon).c_str()), item.substr(colon + 1));
  }
  return out;
}

}  // namespace

int main(int, char**) {
  // Log to the user's settings folder; the console panel shows the same lines.
  std::filesystem::create_directories(settingsDir() / "logs");
  LoggerService::initialize(std::make_unique<Logger>("engine", (settingsDir() / "logs" / "editor.log").string()));
  captureEngineLog();

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
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  gladLoadGL(glfwGetProcAddress);
  glfwSetKeyCallback(window, onKey);  // before ImGui, which chains to it

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
    const auto script = parseScript(env("JM_EDITOR_SCRIPT"));
    const std::string capture = env("JM_EDITOR_CAPTURE");
    const int captureFrame = env("JM_EDITOR_FRAMES").empty() ? 0 : std::atoi(env("JM_EDITOR_FRAMES").c_str());

    double last = glfwGetTime();
    for (int frame = 0;; ++frame) {
      if (glfwWindowShouldClose(window)) {
        glfwSetWindowShouldClose(window, GLFW_FALSE);
        editor.requestQuit();
      }
      if (editor.quitConfirmed()) break;
      glfwPollEvents();
      // Idle politely when minimized.
      if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
      }
      const double now = glfwGetTime();
      const float dt = static_cast<float>(capture.empty() ? now - last : 1.0 / 60.0);
      last = now;

      auto [from, to] = script.equal_range(frame);
      for (auto it = from; it != to; ++it) {
        if (it->second.starts_with("@")) {
          simulate(editor, it->second);
        } else if (!editor.commands().run(it->second)) {
          LogBook::instance().add(LogBook::Level::Warning, LogBook::Source::Editor, "script: '" + it->second + "' didn't run");
        }
      }

      ImGui_ImplOpenGL3_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();
      editor.frame(dt);
      ImGui::Render();

      int fbw = 0, fbh = 0;
      glfwGetFramebufferSize(window, &fbw, &fbh);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, fbw, fbh);
      const ImVec4 bg = theme::bg0;
      glClearColor(bg.x, bg.y, bg.z, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
      if (!capture.empty() && frame == captureFrame) {
        savePng(capture, fbw, fbh);
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
