#pragma once

#include <array>
#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json_fwd.hpp>

// Where frames' time goes: named timings (a system, a script, a module) and
// counts (entities, draw calls) added during a frame, kept per frame for the
// last kWindow frames: their average and worst.
class Profiler {
 public:
  static constexpr size_t kWindow = 120;  // two seconds at 60 fps

  enum class Kind { System, Script, Module, Count };
  // Adds to this frame's total for (kind, name): seconds, or a count.
  void add(Kind kind, std::string_view name, double value);
  // The frame took `seconds`: its totals join the window.
  void endFrame(double seconds);

  // {"frames", "frame": {"avgMs", "worstMs"}, "systems"/"scripts"/"modules":
  // [{"name", "avgMs", "worstMs"}, ...] costliest first, "counts": {name: {"avg", "max"}}}.
  nlohmann::json report() const;

 private:
  struct Series {
    std::array<double, kWindow> values{};
    double pending = 0.0;  // this frame's, so far
  };
  using Key = std::pair<Kind, std::string>;
  std::map<Key, Series> _series;
  Key _probe;  // add()'s lookup key, reused: no allocation per call
  Series _frame;
  size_t _frames = 0;  // frames ended; the window holds the last min(_frames, kWindow)
};
