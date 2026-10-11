#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

class Renderer2D;

// Scripts' debug drawing (Debug.line/circle/rect/text): shapes over the game,
// in world units or screen px (from the top-left, y down), each for one frame
// or some seconds of game time.
class DebugDraw {
 public:
  enum class Shape : int32_t { Line = 0, Circle = 1, Rect = 2, Text = 3 };
  struct Item {
    Shape shape = Shape::Line;
    bool screen = false;
    glm::vec2 a{0.0f};  // a line's start; a circle's, rect's center; where text starts
    glm::vec2 b{0.0f};  // a line's end; a circle's radius (x); a rect's size
    glm::vec4 color{1.0f};
    std::string text;
    float seconds = 0.0f;  // left to show; 0: until the next frame
    bool redrawnWhilePaused = false;  // a runWhenPaused script's: it draws again while paused
  };

  // `color` is 0xRRGGBB; a bad shape or a non-finite number is ignored.
  // redrawnWhilePaused: drawn by a runWhenPaused script.
  void add(Shape shape, bool screen, glm::vec2 a, glm::vec2 b, uint32_t color, float seconds,
           bool redrawnWhilePaused, std::string text = {});
  // As a frame starts, before scripts: last frame's items go and timed ones
  // count down `dt`. While paused, what stopped scripts drew stays.
  void expire(float dt, bool paused);
  void clear() { _items.clear(); }

  // The lines, circles and rects of one space, with the screen quads (over
  // the UI): world ones placed through the renderer's camera. Text is the caller's.
  void drawShapes(Renderer2D& renderer, bool screen) const;
  // Where a world point is on screen, in logical px (y down).
  static glm::vec2 toScreen(Renderer2D& renderer, glm::vec2 world);
  const std::vector<Item>& items() const { return _items; }
  // state["debug"]: each item as data (shape, space, points, color, text, seconds).
  nlohmann::json describe() const;

 private:
  std::vector<Item> _items;
};
