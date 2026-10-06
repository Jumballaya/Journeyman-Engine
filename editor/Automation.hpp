#pragma once

#include <deque>
#include <filesystem>
#include <map>
#include <string>

class Editor;

// Driving the editor from outside: screenshots, smoke tests, and a live
// session another program steers step by step. A step is a command id
// ("scene.save") or simulated input, one step per frame:
//   "@mouse x y", "@down", "@up", "@rdown", "@rup", "@wheel dy", "@key Enter",
//   "@type text", "@ctrl" / "@shift" (held until "@release"), and shorthands
//   "@click x y", "@dblclick x y", "@rclick x y", "@drag x1 y1 x2 y2",
//   "@wait frames", "@shot out.png" (after the frame draws), "@done token";
// plus editor actions: "@select Name", "@open path", "@inspect path",
// "@reveal path", "@import file", "@add asset", "@apply asset", "@move a b",
// "@makeprefab", "@live tag". Points are from the window's top-left.
class Automation {
 public:
  // `script`: "frame:step;frame:step" run at those frames (JM_EDITOR_SCRIPT).
  // `control`: a folder whose file "in" gets steps appended, one per line, by
  // another program; "@done token" writes the token to "done" when reached.
  Automation(const std::string& script, std::filesystem::path control);
  bool live() const { return !_control.empty(); }
  // Before ImGui::NewFrame: this frame's steps.
  void beforeFrame(Editor& editor, int frame);
  // After the frame is drawn into the back buffer: a requested screenshot.
  void afterRender(int framebufferWidth, int framebufferHeight);

 private:
  std::multimap<int, std::string> _timed;
  std::filesystem::path _control;
  std::uintmax_t _read = 0;  // bytes of control/in consumed
  std::string _partial;      // a line not finished yet
  std::deque<std::string> _queue;
  int _wait = 0;
  std::string _shot;

  void readControl();
  bool expand(const std::string& step);  // queues a shorthand's single steps; false if not one
  void run(Editor& editor, const std::string& step);
};

// The back buffer as a PNG (the window's pixels, top row first).
void savePng(const std::string& path, int width, int height);
