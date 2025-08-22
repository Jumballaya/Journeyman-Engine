#pragma once
#include <string>

struct GLFWwindow;

class Window {
 public:
  struct Desc {
    int width{1280};
    int height{720};
    std::string title{"Journeyman Engine"};
    bool resizable{true};
    bool vsync{true};
    int glMajor{3}, glMinor{3};
  };

  Window() = default;
  ~Window();

  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  Window(Window&&) noexcept;
  Window& operator=(Window&&) noexcept;

  void initialize(const Desc& d);
  void poll();
  void present();

  bool shouldClose() const;
  void setVSync(bool on);
  void setTitle(const std::string& t);

  //   glm::ivec2 framebufferSize() const;
  //   glm::ivec2 windowSize() const;

  GLFWwindow* handle() const { return _win; }

 private:
  GLFWwindow* _win = nullptr;
};
