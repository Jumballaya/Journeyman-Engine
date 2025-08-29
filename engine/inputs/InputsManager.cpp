#include "InputsManager.hpp"

#include <algorithm>

static constexpr std::pair<int, inputs::Key> kGLFWKeyToInputsKey[] = {
    // Letters
    {GLFW_KEY_A, inputs::Key::A},
    {GLFW_KEY_B, inputs::Key::B},
    {GLFW_KEY_C, inputs::Key::C},
    {GLFW_KEY_D, inputs::Key::D},
    {GLFW_KEY_E, inputs::Key::E},
    {GLFW_KEY_F, inputs::Key::F},
    {GLFW_KEY_G, inputs::Key::G},
    {GLFW_KEY_H, inputs::Key::H},
    {GLFW_KEY_I, inputs::Key::I},
    {GLFW_KEY_J, inputs::Key::J},
    {GLFW_KEY_K, inputs::Key::K},
    {GLFW_KEY_L, inputs::Key::L},
    {GLFW_KEY_M, inputs::Key::M},
    {GLFW_KEY_N, inputs::Key::N},
    {GLFW_KEY_O, inputs::Key::O},
    {GLFW_KEY_P, inputs::Key::P},
    {GLFW_KEY_Q, inputs::Key::Q},
    {GLFW_KEY_R, inputs::Key::R},
    {GLFW_KEY_S, inputs::Key::S},
    {GLFW_KEY_T, inputs::Key::T},
    {GLFW_KEY_U, inputs::Key::U},
    {GLFW_KEY_V, inputs::Key::V},
    {GLFW_KEY_W, inputs::Key::W},
    {GLFW_KEY_X, inputs::Key::X},
    {GLFW_KEY_Y, inputs::Key::Y},
    {GLFW_KEY_Z, inputs::Key::Z},

    // Top-row digits
    {GLFW_KEY_0, inputs::Key::Digit0},
    {GLFW_KEY_1, inputs::Key::Digit1},
    {GLFW_KEY_2, inputs::Key::Digit2},
    {GLFW_KEY_3, inputs::Key::Digit3},
    {GLFW_KEY_4, inputs::Key::Digit4},
    {GLFW_KEY_5, inputs::Key::Digit5},
    {GLFW_KEY_6, inputs::Key::Digit6},
    {GLFW_KEY_7, inputs::Key::Digit7},
    {GLFW_KEY_8, inputs::Key::Digit8},
    {GLFW_KEY_9, inputs::Key::Digit9},

    // punctuation
    {GLFW_KEY_MINUS, inputs::Key::Minus},
    {GLFW_KEY_EQUAL, inputs::Key::Equal},
    {GLFW_KEY_GRAVE_ACCENT, inputs::Key::Backtick},
    {GLFW_KEY_LEFT_BRACKET, inputs::Key::LeftBracket},
    {GLFW_KEY_RIGHT_BRACKET, inputs::Key::RightBracket},
    {GLFW_KEY_BACKSLASH, inputs::Key::Backslash},
    {GLFW_KEY_SEMICOLON, inputs::Key::Semicolon},
    {GLFW_KEY_APOSTROPHE, inputs::Key::Apostrophe},
    {GLFW_KEY_COMMA, inputs::Key::Comma},
    {GLFW_KEY_PERIOD, inputs::Key::Period},
    {GLFW_KEY_SLASH, inputs::Key::Slash},

    // Function keys
    {GLFW_KEY_F1, inputs::Key::F1},
    {GLFW_KEY_F2, inputs::Key::F2},
    {GLFW_KEY_F3, inputs::Key::F3},
    {GLFW_KEY_F4, inputs::Key::F4},
    {GLFW_KEY_F5, inputs::Key::F5},
    {GLFW_KEY_F6, inputs::Key::F6},
    {GLFW_KEY_F7, inputs::Key::F7},
    {GLFW_KEY_F8, inputs::Key::F8},
    {GLFW_KEY_F9, inputs::Key::F9},
    {GLFW_KEY_F10, inputs::Key::F10},
    {GLFW_KEY_F11, inputs::Key::F11},
    {GLFW_KEY_F12, inputs::Key::F12},
    {GLFW_KEY_F13, inputs::Key::F13},
    {GLFW_KEY_F14, inputs::Key::F14},
    {GLFW_KEY_F15, inputs::Key::F15},
    {GLFW_KEY_F16, inputs::Key::F16},
    {GLFW_KEY_F17, inputs::Key::F17},
    {GLFW_KEY_F18, inputs::Key::F18},
    {GLFW_KEY_F19, inputs::Key::F19},
    {GLFW_KEY_F20, inputs::Key::F20},
    {GLFW_KEY_F21, inputs::Key::F21},
    {GLFW_KEY_F22, inputs::Key::F22},
    {GLFW_KEY_F23, inputs::Key::F23},
    {GLFW_KEY_F24, inputs::Key::F24},

    // Navigation / system
    {GLFW_KEY_ESCAPE, inputs::Key::Escape},
    {GLFW_KEY_TAB, inputs::Key::Tab},
    {GLFW_KEY_ENTER, inputs::Key::Enter},
    {GLFW_KEY_SPACE, inputs::Key::Space},
    {GLFW_KEY_BACKSPACE, inputs::Key::Backspace},
    {GLFW_KEY_INSERT, inputs::Key::Insert},
    {GLFW_KEY_DELETE, inputs::Key::Delete},
    {GLFW_KEY_HOME, inputs::Key::Home},
    {GLFW_KEY_END, inputs::Key::End},
    {GLFW_KEY_PAGE_UP, inputs::Key::PageUp},
    {GLFW_KEY_PAGE_DOWN, inputs::Key::PageDown},
    {GLFW_KEY_UP, inputs::Key::ArrowUp},
    {GLFW_KEY_DOWN, inputs::Key::ArrowDown},
    {GLFW_KEY_LEFT, inputs::Key::ArrowLeft},
    {GLFW_KEY_RIGHT, inputs::Key::ArrowRight},
    {GLFW_KEY_CAPS_LOCK, inputs::Key::CapsLock},
    {GLFW_KEY_NUM_LOCK, inputs::Key::NumLock},
    {GLFW_KEY_SCROLL_LOCK, inputs::Key::ScrollLock},
    {GLFW_KEY_PRINT_SCREEN, inputs::Key::PrintScreen},
    {GLFW_KEY_PAUSE, inputs::Key::Pause},

    // Keypad
    {GLFW_KEY_KP_0, inputs::Key::KP0},
    {GLFW_KEY_KP_1, inputs::Key::KP1},
    {GLFW_KEY_KP_2, inputs::Key::KP2},
    {GLFW_KEY_KP_3, inputs::Key::KP3},
    {GLFW_KEY_KP_4, inputs::Key::KP4},
    {GLFW_KEY_KP_5, inputs::Key::KP5},
    {GLFW_KEY_KP_6, inputs::Key::KP6},
    {GLFW_KEY_KP_7, inputs::Key::KP7},
    {GLFW_KEY_KP_8, inputs::Key::KP8},
    {GLFW_KEY_KP_9, inputs::Key::KP9},
    {GLFW_KEY_KP_DECIMAL, inputs::Key::KPPeriod},
    {GLFW_KEY_KP_ENTER, inputs::Key::KPEnter},
    {GLFW_KEY_KP_ADD, inputs::Key::KPAdd},
    {GLFW_KEY_KP_SUBTRACT, inputs::Key::KPSubtract},
    {GLFW_KEY_KP_MULTIPLY, inputs::Key::KPMultiply},
    {GLFW_KEY_KP_DIVIDE, inputs::Key::KPDivide},
};

void InputsManager::initialize(EventBus& eventBus) {
  for (auto& state : _keyState) {
    state.down = false;
    state.pressed = false;
    state.released = false;
    state.lastChangedFrame = 0;
    state.timeDownStart = 0.0f;
  }

  for (auto& state : _mouseState.buttons) {
    state.down = false;
    state.pressed = false;
    state.released = false;
    state.lastChangedFrame = 0;
    state.timeDownStart = 0.0f;
  }
  _mouseState.delta = glm::vec2{0.0f};
  _mouseState.position = glm::vec2{0.0f};
  _mouseState.wheel = glm::vec2{0.0f};
  _mouseState.insideWindow = false;
  _mouseState.locked = false;

  _modifiers = 0;

  int maxScancode = 0;
  for (auto& [glfwKey, _] : kGLFWKeyToInputsKey) {
    _keyToKey[glfwKey] = inputs::Key::Key_Invalid;
    int sc = glfwGetKeyScancode(glfwKey);
    maxScancode = std::max(sc, maxScancode);
  }
  _scanToKey.resize(maxScancode + 1);
  for (int i = 0; i < maxScancode; ++i) {
    _scanToKey[i] = inputs::Key::Key_Invalid;
  }

  for (auto& [glfwKey, inputsKey] : kGLFWKeyToInputsKey) {
    int sc = glfwGetKeyScancode(glfwKey);
    _keyToKey[glfwKey] = inputsKey;
    if (sc > 0) {
      _scanToKey[sc] = inputsKey;
    }
  }
}