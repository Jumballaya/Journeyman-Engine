#pragma once

#include <atomic>
#include <cmath>

// Frame timing. scale multiplies dt (0 = paused; runWhenPaused scripts get unscaled
// dt); atomic because scripts set it from worker threads.
class GameClock {
 public:
  void advance(float unscaledDt) {
    _unscaledDt = unscaledDt;
    _scaledDt = unscaledDt * scale();
    _unscaledElapsed += unscaledDt;
    _elapsed += _scaledDt;
  }

  float scale() const { return _scale.load(std::memory_order_relaxed); }
  // NaN/inf/negative become 0 (paused) so a bad value can't poison every dt.
  void setScale(float s) { _scale.store(std::isfinite(s) && s > 0.0f ? s : 0.0f, std::memory_order_relaxed); }
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
