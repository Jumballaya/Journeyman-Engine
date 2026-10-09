#include "RemoteInput.hpp"

#include <cmath>

void RemoteInput::apply(const InputSnapshot& snapshot) {
  for (uint16_t k = 0; k < inputs::Key::Key_Count; ++k) {
    const auto key = static_cast<inputs::Key>(k);
    const bool was = _last.keyDown(key), is = snapshot.keyDown(key);
    if (is && !was) _keys.registerKeyDown(key);
    if (!is && was) _keys.registerKeyUp(key);
  }
  for (auto& [name, state] : _actions) {
    if (!state.down) continue;
    // Gone from the snapshot: unbound there, so up.
    bool listed = false;
    for (const auto& a : snapshot.actions) listed = listed || a.name == name;
    if (!listed) {
      state.down = false;
      state.released = true;
      state.value = 0.0f;
    }
  }
  for (const auto& a : snapshot.actions) {
    ActionState& state = _actions[a.name];
    if (a.down && !state.down) {
      state.pressed = true;
      state.held = 0.0f;
    }
    if (!a.down && state.down) state.released = true;
    state.down = a.down;
    state.value = a.value;
  }
  _last = snapshot;
}

void RemoteInput::tick(float dt) {
  _keys.tick(dt);
  _dt = _keys.frameTime();
  // Held time counts from the press, as a local key's does.
  for (auto& [name, state] : _actions) {
    if (state.down) state.held += _dt;
    state.pressed = state.released = false;
  }
}

const RemoteInput::ActionState* RemoteInput::find(const std::string& action) const {
  auto it = _actions.find(action);
  return it == _actions.end() ? nullptr : &it->second;
}

bool RemoteInput::down(const std::string& action) const {
  const ActionState* s = find(action);
  return s && s->down;
}
bool RemoteInput::pressed(const std::string& action) const {
  const ActionState* s = find(action);
  return s && s->pressed;
}
bool RemoteInput::released(const std::string& action) const {
  const ActionState* s = find(action);
  return s && s->released;
}
float RemoteInput::value(const std::string& action) const {
  const ActionState* s = find(action);
  return s ? s->value : 0.0f;
}

bool RemoteInput::repeated(const std::string& action, float delay, float interval) const {
  const ActionState* s = find(action);
  if (!s) return false;
  if (s->pressed) return true;
  if (!s->down || s->held < delay) return false;
  const float before = s->held - _dt;
  if (before < delay || interval <= 0.0f) return true;
  return std::floor((s->held - delay) / interval) != std::floor((before - delay) / interval);
}
