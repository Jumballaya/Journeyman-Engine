#include "InputActions.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

#include "../core/logger/logging.hpp"

namespace inputs {
namespace {

constexpr std::string_view kKeyNames[] = {
#define JM_KEY_NAME(name) #name,
    JM_INPUT_KEYS(JM_KEY_NAME)
#undef JM_KEY_NAME
};

constexpr std::string_view kPadNames[] = {
    "A", "B", "X", "Y", "LeftBumper", "RightBumper", "Back", "Start", "Guide", "LeftThumb", "RightThumb",
    "DPadUp", "DPadRight", "DPadDown", "DPadLeft",
    "LeftStickLeft", "LeftStickRight", "LeftStickUp", "LeftStickDown",
    "RightStickLeft", "RightStickRight", "RightStickUp", "RightStickDown",
    "LeftTrigger", "RightTrigger"};
static_assert(std::size(kPadNames) == static_cast<size_t>(Pad::Count), "kPadNames must match inputs::Pad");

constexpr std::string_view kPadPrefix = "Gamepad.";

}  // namespace

std::optional<Control> parseControl(std::string_view name) {
  if (name.starts_with(kPadPrefix)) {
    name.remove_prefix(kPadPrefix.size());
    for (size_t i = 0; i < std::size(kPadNames); ++i) {
      if (kPadNames[i] == name) return Control{static_cast<Pad>(i)};
    }
    return std::nullopt;
  }
  for (size_t i = 0; i < std::size(kKeyNames); ++i) {
    if (kKeyNames[i] == name) return Control{static_cast<Key>(i)};
  }
  return std::nullopt;
}

std::vector<Control> parseControls(std::string_view name) {
  constexpr std::pair<std::string_view, std::pair<Key, Key>> kEitherSide[] = {
      {"Shift", {Key::LeftShift, Key::RightShift}},
      {"Ctrl", {Key::LeftCtrl, Key::RightCtrl}},
      {"Alt", {Key::LeftAlt, Key::RightAlt}},
      {"Super", {Key::LeftSuper, Key::RightSuper}},
  };
  for (const auto& [alias, keys] : kEitherSide) {
    if (name == alias) return {keys.first, keys.second};
  }
  if (auto control = parseControl(name)) return {*control};
  return {};
}

std::string_view keyName(Key key) {
  return key < Key::Key_Count ? kKeyNames[key] : std::string_view{};
}

}  // namespace inputs

void InputActions::loadBindings(const nlohmann::json& json, std::string_view source) {
  if (!json.contains("actions") || !json["actions"].is_object()) {
    JM_LOG_ERROR("[Inputs] {}: expected an \"actions\" object", source);
    return;
  }
  std::lock_guard lock(_mutex);
  for (const auto& [action, controls] : json["actions"].items()) {
    std::vector<inputs::Control> parsed;
    for (const auto& c : controls) {
      if (!c.is_string()) continue;
      const auto name = c.get<std::string>();
      const auto matched = inputs::parseControls(name);
      if (matched.empty()) JM_LOG_WARN("[Inputs] {}: unknown control '{}' for action '{}'", source, name, action);
      parsed.insert(parsed.end(), matched.begin(), matched.end());
    }
    _actions[action] = std::move(parsed);
  }
}

bool InputActions::bind(const std::string& action, std::string_view control) {
  auto parsed = inputs::parseControls(control);
  if (parsed.empty()) return false;
  std::lock_guard lock(_mutex);
  auto& bound = _actions[action];
  bound.insert(bound.end(), parsed.begin(), parsed.end());
  return true;
}

void InputActions::unbind(const std::string& action) {
  std::lock_guard lock(_mutex);
  _actions.erase(action);
}

template <typename KeyPred, typename PadPred>
bool InputActions::any(const std::string& action, KeyPred keyPred, PadPred padPred) const {
  std::lock_guard lock(_mutex);
  auto it = _actions.find(action);
  if (it == _actions.end()) return false;
  for (const auto& control : it->second) {
    if (const auto* key = std::get_if<inputs::Key>(&control)) {
      if (keyPred(*key)) return true;
    } else if (padPred(static_cast<size_t>(std::get<inputs::Pad>(control)))) {
      return true;
    }
  }
  return false;
}

bool InputActions::down(const std::string& action, const InputsManager& keys) const {
  return any(action, [&](inputs::Key k) { return keys.keyIsDown(k); },
             [&](size_t p) { return _pad.down[p]; });
}

bool InputActions::pressed(const std::string& action, const InputsManager& keys) const {
  return any(action, [&](inputs::Key k) { return keys.keyIsPressed(k); },
             [&](size_t p) { return _pad.pressed[p]; });
}

bool InputActions::released(const std::string& action, const InputsManager& keys) const {
  return any(action, [&](inputs::Key k) { return keys.keyIsReleased(k); },
             [&](size_t p) { return _pad.released[p]; });
}

float InputActions::value(const std::string& action, const InputsManager& keys) const {
  float best = 0.0f;
  any(action, [&](inputs::Key k) { if (keys.keyIsDown(k)) best = 1.0f; return false; },
      [&](size_t p) { best = std::max(best, _pad.value[p]); return false; });
  return best;
}

namespace {

// Whether a control held for `held` seconds (`dt` more than last frame)
// crossed a repeat point: `delay`, then every `interval` after it.
bool repeatsNow(float held, float dt, float delay, float interval) {
  if (held < delay) return false;
  const float before = held - dt;
  if (before < delay) return true;
  if (interval <= 0.0f) return true;
  return std::floor((held - delay) / interval) != std::floor((before - delay) / interval);
}

}  // namespace

bool InputActions::repeated(const std::string& action, const InputsManager& keys, float delay, float interval) const {
  return any(action,
             [&](inputs::Key k) {
               return keys.keyIsPressed(k) ||
                      (keys.keyIsDown(k) && repeatsNow(keys.heldFor(k), keys.frameTime(), delay, interval));
             },
             [&](size_t p) {
               return _pad.pressed[p] || (_pad.down[p] && repeatsNow(_pad.held[p], _padDt, delay, interval));
             });
}

void InputActions::pollGamepads(float dt) {
  using inputs::Pad;
  constexpr float kDeadzone = 0.25f;
  constexpr float kPressThreshold = 0.5f;

  std::array<float, kPadCount> value{};
  bool connected = false;

  for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
    GLFWgamepadstate state;
    if (!glfwJoystickIsGamepad(jid) || !glfwGetGamepadState(jid, &state)) continue;
    connected = true;

    auto set = [&](Pad p, float v) {
      auto& slot = value[static_cast<size_t>(p)];
      slot = std::max(slot, v);
    };
    // GLFW button order matches Pad::A..Pad::DPadLeft exactly.
    for (int b = 0; b <= GLFW_GAMEPAD_BUTTON_LAST; ++b) {
      if (state.buttons[b] == GLFW_PRESS) set(static_cast<Pad>(b), 1.0f);
    }
    auto stick = [&](float axis, Pad negative, Pad positive) {
      const float a = std::fabs(axis) < kDeadzone ? 0.0f : (std::fabs(axis) - kDeadzone) / (1.0f - kDeadzone);
      if (axis < 0) set(negative, a);
      if (axis > 0) set(positive, a);
    };
    stick(state.axes[GLFW_GAMEPAD_AXIS_LEFT_X], Pad::LeftStickLeft, Pad::LeftStickRight);
    stick(state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y], Pad::LeftStickUp, Pad::LeftStickDown);
    stick(state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X], Pad::RightStickLeft, Pad::RightStickRight);
    stick(state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y], Pad::RightStickUp, Pad::RightStickDown);
    // Triggers rest at -1.
    set(Pad::LeftTrigger, (state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] + 1.0f) * 0.5f);
    set(Pad::RightTrigger, (state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1.0f) * 0.5f);
  }

  for (size_t i = 0; i < value.size(); ++i) {
    const bool wasDown = _pad.down[i];
    const bool isDown = value[i] >= kPressThreshold;
    _pad.value[i] = value[i];
    _pad.down[i] = isDown;
    _pad.pressed[i] = isDown && !wasDown;
    _pad.released[i] = !isDown && wasDown;
    _pad.held[i] = isDown && wasDown ? _pad.held[i] + dt : 0.0f;
  }
  _padDt = dt;
  if (connected != _padConnected) {
    JM_LOG_INFO("[Inputs] gamepad {}", connected ? "connected" : "disconnected");
  }
  _padConnected = connected;
}
