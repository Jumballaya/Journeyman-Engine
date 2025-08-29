#pragma once

#include <GLFW/glfw3.h>

#include <array>
#include <bitset>
#include <cstdint>
#include <glm/glm.hpp>
#include <utility>
#include <vector>

#include "../core/async/LockFreeQueue.hpp"
#include "../core/events/EventBus.hpp"

namespace inputs {

enum Key : uint16_t {
  A,
  B,
  C,
  D,
  E,
  F,
  G,
  H,
  I,
  J,
  K,
  L,
  M,
  N,
  O,
  P,
  Q,
  R,
  S,
  T,
  U,
  V,
  W,
  X,
  Y,
  Z,
  Digit0,
  Digit1,
  Digit2,
  Digit3,
  Digit4,
  Digit5,
  Digit6,
  Digit7,
  Digit8,
  Digit9,
  Minus,
  Equal,
  Backtick,
  LeftBracket,
  RightBracket,
  Backslash,
  Semicolon,
  Apostrophe,
  Comma,
  Period,
  Slash,
  F1,
  F2,
  F3,
  F4,
  F5,
  F6,
  F7,
  F8,
  F9,
  F10,
  F11,
  F12,
  F13,
  F14,
  F15,
  F16,
  F17,
  F18,
  F19,
  F20,
  F21,
  F22,
  F23,
  F24,
  Escape,
  Tab,
  Enter,
  Space,
  Backspace,
  Insert,
  Delete,
  Home,
  End,
  PageUp,
  PageDown,
  ArrowUp,
  ArrowDown,
  ArrowLeft,
  ArrowRight,
  CapsLock,
  NumLock,
  ScrollLock,
  PrintScreen,
  Pause,
  KP0,
  KP1,
  KP2,
  KP3,
  KP4,
  KP5,
  KP6,
  KP7,
  KP8,
  KP9,
  KPPeriod,
  KPEnter,
  KPAdd,
  KPSubtract,
  KPMultiply,
  KPDivide,

  Key_Count,  // Last _real_ key
  Key_Invalid = 0xFFFF,
};

enum Mod : uint8_t {
  Shift = 1 << 0,
  Ctrl = 1 << 1,
  Alt = 1 << 2,
  Super = 1 << 3,
  Caps = 1 << 4,
  Num = 1 << 5
};

}  // namespace inputs

struct KeyState {
  bool down;                  // currently held down
  bool pressed;               // went from up -> down this frame
  bool released;              // went from down -> up this frame
  uint64_t lastChangedFrame;  // last frame that the state changed
  float timeDownStart;        // held for N seconds
};

struct MouseState {
  glm::vec2 position;               // current mouse position
  glm::vec2 delta;                  // delta position from last frame
  glm::vec2 wheel;                  // delta wheel value per frame
  std::array<KeyState, 8> buttons;  // button state
  bool locked;                      // is the mouse locked
  bool insideWindow;                // is the mouse inside the window
};

class InputsManager {
 public:
  InputsManager() = default;
  ~InputsManager() = default;

  void initialize(EventBus& eventBus);
  void tick(float dt);

  inputs::Key keyFromScancode(int scancode) const;

  void registerKeyDown(inputs::Key key);
  void registerKeyUp(inputs::Key key);
  void registerKeyRepeat(inputs::Key key);

  bool keyIsPressed(inputs::Key key) const;
  bool keyIsReleased(inputs::Key key) const;
  bool keyIsDown(inputs::Key key) const;
  bool keyIsUp(inputs::Key key) const;

  //   void registerMouseDown(int button);
  //   void registerMouseUp(int button);
  //   void registerMouseMove(glm::vec2 pos);
  //   void registerMouseWheel(glm::vec2 delta);
  //   void registerMouseLock(bool lock);
  //   void registerMouseInsideWindow(bool inside);

  const MouseState& getMouseState() const;
  const KeyState& getKeyState(inputs::Key key) const;

 private:
  std::array<KeyState, inputs::Key::Key_Count> _keyState;
  MouseState _mouseState;
  uint8_t _modifiers;  // uses inputs::Mod enum

  std::vector<inputs::Key> _scanToKey;  // needs to be dynamic due to how GLFW creates its scan code list
  std::array<inputs::Key, GLFW_KEY_LAST + 1> _keyToKey;

  uint64_t _currentFrame;
  double _nowSeconds;
};
