#pragma once

#include <atomic>

// Frame timing shared by the engine loop, systems and scripts.
//
// `scale` multiplies wall-clock dt before it reaches systems; scale == 0 means
// the game is paused. Scripts flagged runWhenPaused (menus, pause overlays)
// keep receiving unscaled dt so UI keeps animating while gameplay is frozen.
// The scale is atomic because scripts set it from worker threads.
class GameClock {
 public:
  void advance(float unscaledDt) {
    _unscaledDt = unscaledDt;
    _scaledDt = unscaledDt * scale();
    _unscaledElapsed += unscaledDt;
    _elapsed += _scaledDt;
  }

  float scale() const { return _scale.load(std::memory_order_relaxed); }
  void setScale(float s) { _scale.store(s < 0.0f ? 0.0f : s, std::memory_order_relaxed); }
  bool paused() const { return scale() == 0.0f; }

  float dt() const { return _scaledDt; }
  float unscaledDt() const { return _unscaledDt; }
  double elapsed() const { return _elapsed; }
  double unscaledElapsed() const { return _unscaledElapsed; }

 private:
  std::atomic<float> _scale{1.0f};
  float _scaledDt = 0.0f;
  float _unscaledDt = 0.0f;
  double _elapsed = 0.0;
  double _unscaledElapsed = 0.0;
};
