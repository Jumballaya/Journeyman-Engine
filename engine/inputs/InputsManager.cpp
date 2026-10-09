#include "InputsManager.hpp"

#include <algorithm>
#include <cmath>

using inputs::Key;

InputsManager::KeyState InputsManager::state(Key key) const {
  return key < Key::Key_Count ? _keyState[key] : KeyState{};
}

// Key repeats arrive here too, so a key held while the window gained focus still goes down.
void InputsManager::registerKeyDown(Key key) {
  if (key >= Key::Key_Count || _keyState[key].down) return;
  auto& s = _keyState[key];
  s.down = s.pressed = true;
  s.downSince = _nowSeconds;
}

void InputsManager::registerKeyUp(Key key) {
  if (key >= Key::Key_Count || !_keyState[key].down) return;
  _keyState[key].down = false;
  _keyState[key].released = true;
}

bool InputsManager::keyIsPressed(Key key) const { return state(key).pressed; }
bool InputsManager::keyIsReleased(Key key) const { return state(key).released; }
bool InputsManager::keyIsDown(Key key) const { return state(key).down; }

float InputsManager::heldFor(Key key) const {
  const KeyState s = state(key);
  return s.down ? static_cast<float>(_nowSeconds - s.downSince) : 0.0f;
}

void InputsManager::tick(float dt) {
  // Clamp big steps to keep input logic sane after stalls.
  constexpr float kMaxDt = 1.0f / 20.0f;
  dt = std::isfinite(dt) ? std::clamp(dt, 0.0f, kMaxDt) : 0.0f;
  _nowSeconds += dt;
  _lastDt = dt;
  for (auto& s : _keyState) s.pressed = s.released = false;
  _frameWheel = _wheel;
  _wheel = glm::vec2{0.0f};
}
