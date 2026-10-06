#pragma once
#include <functional>
#include <string>

struct GLFWwindow;

class Window {
 public:
  using ResizeCallback = std::function<void(int, int)>;
  using KeyCallback = std::function<void(int, int, int, int)>;
  using MouseMoveCallback = std::function<void(float, float)>;  // framebuffer px, top-left origin
  using MouseButtonCallback = std::function<void(int, bool)>;
  using ScrollCallback = std::function<void(float, float)>;

  struct Desc {
    int width{1280};
    int height{720};
    std::string title{"Journeyman Engine"};
    bool resizable{true};
    bool vsync{true};
    bool visible{true};       // false = offscreen (automated runs)
    bool fullscreen{false};   // borderless on the primary monitor
    bool hideCursor{false};
#ifdef __APPLE__
    int glMajor{4}, glMinor{1};
#else
    int glMajor{4}, glMinor{6};
#endif
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
  void setFullscreen(bool on);
  bool isFullscreen() const { return _fullscreen; }
  bool isFocused() const;
  void setTitle(const std::string& t);

  void setResizeCallback(ResizeCallback callback);
  void setKeyCallback(KeyCallback callback);
  void setMouseCallbacks(MouseMoveCallback move, MouseButtonCallback button, ScrollCallback scroll);

  GLFWwindow* handle() const { return _win; }

  void destroy();

 private:
  GLFWwindow* _win = nullptr;
  ResizeCallback _resizeCallback;
  KeyCallback _keyCallback;
  MouseMoveCallback _mouseMoveCallback;
  MouseButtonCallback _mouseButtonCallback;
  ScrollCallback _scrollCallback;
  Desc _descriptor;

  int _width = 0;
  int _height = 0;
  bool _fullscreen = false;
  int _windowedX = 100, _windowedY = 100, _windowedW = 0, _windowedH = 0;

  static void handleResize(GLFWwindow* window, int width, int height);
  static void handleKey(GLFWwindow* window, int key, int scancode, int action, int mods);
  static void handleCursor(GLFWwindow* window, double x, double y);
  static void handleMouseButton(GLFWwindow* window, int button, int action, int mods);
  static void handleScroll(GLFWwindow* window, double dx, double dy);
};
