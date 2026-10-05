#pragma once

#include <functional>
#include <string>
#include <vector>

// Short, non-blocking notices stacked in the bottom-right corner. Errors stay
// until dismissed; the rest fade after a few seconds.
class Toasts {
 public:
  enum class Kind { Info, Success, Warning, Error };

  void show(Kind kind, std::string title, std::string body = {}, std::string action = {},
            std::function<void()> onAction = {});
  void draw();

 private:
  struct Toast {
    Kind kind;
    std::string title, body, action;
    std::function<void()> onAction;
    double born;
    bool dismissed = false;
    float height = 0;  // measured last frame, for stacking
  };
  std::vector<Toast> _toasts;
};
