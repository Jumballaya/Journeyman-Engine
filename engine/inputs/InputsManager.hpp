#pragma once

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace inputs {

// Every key, in inputs::Key order; KEY(name) also gives its control name ("Space").
// Mouse buttons are keys too, so actions bind them like any other.
#define JM_INPUT_KEYS(KEY) \
  KEY(A) KEY(B) KEY(C) KEY(D) KEY(E) KEY(F) KEY(G) KEY(H) KEY(I) KEY(J) KEY(K) KEY(L) KEY(M) \
  KEY(N) KEY(O) KEY(P) KEY(Q) KEY(R) KEY(S) KEY(T) KEY(U) KEY(V) KEY(W) KEY(X) KEY(Y) KEY(Z) \
  KEY(Digit0) KEY(Digit1) KEY(Digit2) KEY(Digit3) KEY(Digit4) KEY(Digit5) KEY(Digit6) KEY(Digit7) \
  KEY(Digit8) KEY(Digit9) \
  KEY(Minus) KEY(Equal) KEY(Backtick) KEY(LeftBracket) KEY(RightBracket) KEY(Backslash) KEY(Semicolon) \
  KEY(Apostrophe) KEY(Comma) KEY(Period) KEY(Slash) \
  KEY(F1) KEY(F2) KEY(F3) KEY(F4) KEY(F5) KEY(F6) KEY(F7) KEY(F8) KEY(F9) KEY(F10) KEY(F11) KEY(F12) \
  KEY(F13) KEY(F14) KEY(F15) KEY(F16) KEY(F17) KEY(F18) KEY(F19) KEY(F20) KEY(F21) KEY(F22) KEY(F23) \
  KEY(F24) \
  KEY(Escape) KEY(Tab) KEY(Enter) KEY(Space) KEY(Backspace) KEY(Insert) KEY(Delete) KEY(Home) KEY(End) \
  KEY(PageUp) KEY(PageDown) \
  KEY(ArrowUp) KEY(ArrowDown) KEY(ArrowLeft) KEY(ArrowRight) KEY(CapsLock) KEY(NumLock) KEY(ScrollLock) \
  KEY(PrintScreen) KEY(Pause) \
  KEY(KP0) KEY(KP1) KEY(KP2) KEY(KP3) KEY(KP4) KEY(KP5) KEY(KP6) KEY(KP7) KEY(KP8) KEY(KP9) \
  KEY(KPPeriod) KEY(KPEnter) KEY(KPAdd) KEY(KPSubtract) KEY(KPMultiply) KEY(KPDivide) \
  KEY(LeftShift) KEY(RightShift) KEY(LeftCtrl) KEY(RightCtrl) KEY(LeftAlt) KEY(RightAlt) KEY(LeftSuper) \
  KEY(RightSuper) \
  KEY(MouseLeft) KEY(MouseRight) KEY(MouseMiddle)

enum Key : uint16_t {
#define JM_KEY_ENUM(name) name,
  JM_INPUT_KEYS(JM_KEY_ENUM)
#undef JM_KEY_ENUM
  Key_Count,
  Key_Invalid = 0xFFFF,
};

}  // namespace inputs

// Keyboard and mouse-button state per frame. Out-of-range keys read as up.
class InputsManager {
 public:
  // Clears last frame's pressed/released edges and wheel.
  void tick(float dt);

  void registerKeyDown(inputs::Key key);
  void registerKeyUp(inputs::Key key);

  bool keyIsPressed(inputs::Key key) const;
  bool keyIsReleased(inputs::Key key) const;
  bool keyIsDown(inputs::Key key) const;
  // Seconds the key has been held (0 on the frame it went down, or if up).
  float heldFor(inputs::Key key) const;
  // The last tick's dt: how far heldFor() advanced since the previous frame.
  float frameTime() const { return _lastDt; }

  // The wheel accumulates over a frame; wheel() is the last finished frame's scroll.
  void registerWheel(float dx, float dy) { _wheel += glm::vec2(dx, dy); }
  glm::vec2 wheel() const { return _frameWheel; }

 private:
  struct KeyState {
    bool down = false;
    bool pressed = false;   // went up -> down this frame
    bool released = false;  // went down -> up this frame
    double downSince = 0.0;
  };
  KeyState state(inputs::Key key) const;

  std::array<KeyState, inputs::Key::Key_Count> _keyState{};
  glm::vec2 _wheel{0.0f};
  glm::vec2 _frameWheel{0.0f};

  double _nowSeconds = 0.0;
  float _lastDt = 0.0f;
};
