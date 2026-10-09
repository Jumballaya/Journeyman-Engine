#include <GLFW/glfw3.h>

#include <array>

#include "../Devices.hpp"

// The keyboard and gamepads, through GLFW (the game and the editor).
namespace inputs::devices {
namespace {

// Keys whose GLFW codes aren't a contiguous run in inputs::Key order.
constexpr std::pair<int, Key> kGLFWKeys[] = {
    {GLFW_KEY_MINUS, Key::Minus},
    {GLFW_KEY_EQUAL, Key::Equal},
    {GLFW_KEY_GRAVE_ACCENT, Key::Backtick},
    {GLFW_KEY_LEFT_BRACKET, Key::LeftBracket},
    {GLFW_KEY_RIGHT_BRACKET, Key::RightBracket},
    {GLFW_KEY_BACKSLASH, Key::Backslash},
    {GLFW_KEY_SEMICOLON, Key::Semicolon},
    {GLFW_KEY_APOSTROPHE, Key::Apostrophe},
    {GLFW_KEY_COMMA, Key::Comma},
    {GLFW_KEY_PERIOD, Key::Period},
    {GLFW_KEY_SLASH, Key::Slash},

    {GLFW_KEY_ESCAPE, Key::Escape},
    {GLFW_KEY_TAB, Key::Tab},
    {GLFW_KEY_ENTER, Key::Enter},
    {GLFW_KEY_SPACE, Key::Space},
    {GLFW_KEY_BACKSPACE, Key::Backspace},
    {GLFW_KEY_INSERT, Key::Insert},
    {GLFW_KEY_DELETE, Key::Delete},
    {GLFW_KEY_HOME, Key::Home},
    {GLFW_KEY_END, Key::End},
    {GLFW_KEY_PAGE_UP, Key::PageUp},
    {GLFW_KEY_PAGE_DOWN, Key::PageDown},
    {GLFW_KEY_UP, Key::ArrowUp},
    {GLFW_KEY_DOWN, Key::ArrowDown},
    {GLFW_KEY_LEFT, Key::ArrowLeft},
    {GLFW_KEY_RIGHT, Key::ArrowRight},
    {GLFW_KEY_CAPS_LOCK, Key::CapsLock},
    {GLFW_KEY_NUM_LOCK, Key::NumLock},
    {GLFW_KEY_SCROLL_LOCK, Key::ScrollLock},
    {GLFW_KEY_PRINT_SCREEN, Key::PrintScreen},
    {GLFW_KEY_PAUSE, Key::Pause},

    {GLFW_KEY_KP_DECIMAL, Key::KPPeriod},
    {GLFW_KEY_KP_ENTER, Key::KPEnter},
    {GLFW_KEY_KP_ADD, Key::KPAdd},
    {GLFW_KEY_KP_SUBTRACT, Key::KPSubtract},
    {GLFW_KEY_KP_MULTIPLY, Key::KPMultiply},
    {GLFW_KEY_KP_DIVIDE, Key::KPDivide},

    {GLFW_KEY_LEFT_SHIFT, Key::LeftShift},
    {GLFW_KEY_RIGHT_SHIFT, Key::RightShift},
    {GLFW_KEY_LEFT_CONTROL, Key::LeftCtrl},
    {GLFW_KEY_RIGHT_CONTROL, Key::RightCtrl},
    {GLFW_KEY_LEFT_ALT, Key::LeftAlt},
    {GLFW_KEY_RIGHT_ALT, Key::RightAlt},
    {GLFW_KEY_LEFT_SUPER, Key::LeftSuper},
    {GLFW_KEY_RIGHT_SUPER, Key::RightSuper},
};

std::vector<Key> scanToKey;  // sized by GLFW's largest scancode
std::array<Key, GLFW_KEY_LAST + 1> keyToKey{};

void buildKeyMaps() {
  keyToKey.fill(Key::Key_Invalid);
  scanToKey.clear();
  auto map = [&](int glfwKey, Key key) {
    keyToKey[glfwKey] = key;
    const int sc = glfwGetKeyScancode(glfwKey);
    if (sc < 0) return;
    if (sc >= static_cast<int>(scanToKey.size())) scanToKey.resize(sc + 1, Key::Key_Invalid);
    scanToKey[sc] = key;
  };
  auto mapRun = [&](int firstGlfwKey, Key first, Key last) {
    for (int i = 0; i <= last - first; ++i) map(firstGlfwKey + i, static_cast<Key>(first + i));
  };
  mapRun(GLFW_KEY_A, Key::A, Key::Z);
  mapRun(GLFW_KEY_0, Key::Digit0, Key::Digit9);
  mapRun(GLFW_KEY_F1, Key::F1, Key::F24);
  mapRun(GLFW_KEY_KP_0, Key::KP0, Key::KP9);
  for (const auto& [glfwKey, key] : kGLFWKeys) map(glfwKey, key);
}

}  // namespace

Key keyFromEvent(int scancode, int glfwKey) {
  static const bool built = (buildKeyMaps(), true);
  (void)built;
  // Scancodes are physical positions (WASD stays WASD on AZERTY); fall back
  // to the layout key code for keys without a mapped scancode.
  if (scancode >= 0 && scancode < static_cast<int>(scanToKey.size()) && scanToKey[scancode] != Key::Key_Invalid) {
    return scanToKey[scancode];
  }
  if (glfwKey >= 0 && glfwKey <= GLFW_KEY_LAST) return keyToKey[glfwKey];
  return Key::Key_Invalid;
}

std::vector<InputActions::GamepadReading> readGamepads() {
  std::vector<InputActions::GamepadReading> pads;
  for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
    GLFWgamepadstate state;
    if (!glfwJoystickIsGamepad(jid) || !glfwGetGamepadState(jid, &state)) continue;
    InputActions::GamepadReading& pad = pads.emplace_back();
    for (int b = 0; b <= GLFW_GAMEPAD_BUTTON_LAST; ++b) pad.buttons[b] = state.buttons[b] == GLFW_PRESS;
    for (int a = 0; a <= GLFW_GAMEPAD_AXIS_LAST; ++a) pad.axes[a] = state.axes[a];
  }
  return pads;
}

}  // namespace inputs::devices
