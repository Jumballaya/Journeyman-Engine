#pragma once
#include <functional>
#include <string>

struct GLFWwindow;

class Window {
 public:
  using ResizeCallback = std::function<void(int, int)>;

  struct Desc {
    int width{1280};
    int height{720};
    std::string title{"Journeyman Engine"};
    bool resizable{true};
    bool vsync{true};
    int glMajor{4}, glMinor{6};
  };

  Window() = default;
  ~Window();

  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  Window(Window&&) = delete;
  Window& operator=(Window&&) = delete;

  void initialize(const Desc& d);
  void poll();
  void present();

  bool shouldClose() const;
  void setVSync(bool on);
  void setTitle(const std::string& t);

  void setResizeCallback(ResizeCallback callback);

  GLFWwindow* handle() const { return _win; }

 private:
  GLFWwindow* _win = nullptr;
  ResizeCallback _resizeCallback;
  Desc _descriptor;

  int _width;
  int _height;

  static void handleResize(GLFWwindow* window, int width, int height);
};
