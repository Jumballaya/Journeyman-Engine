#pragma once

#include <array>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "InputsManager.hpp"

namespace inputs {

// Gamepad controls as virtual buttons. Stick directions and triggers are
// analog (value 0..1); everything else is 0 or 1.
enum class Pad : uint8_t {
  A, B, X, Y, LeftBumper, RightBumper, Back, Start, Guide, LeftThumb, RightThumb,
  DPadUp, DPadRight, DPadDown, DPadLeft,
  LeftStickLeft, LeftStickRight, LeftStickUp, LeftStickDown,
  RightStickLeft, RightStickRight, RightStickUp, RightStickDown,
  LeftTrigger, RightTrigger,
  Count
};

// Parses "Space", "ArrowLeft", "Gamepad.A", "Gamepad.LeftStickLeft", ...
using Control = std::variant<Key, Pad>;
std::optional<Control> parseControl(std::string_view name);
// parseControl, plus "Shift", "Ctrl", "Alt" and "Super" for either side's key.
std::vector<Control> parseControls(std::string_view name);
std::string_view keyName(Key key);

}  // namespace inputs

// Named actions ("fire") bound to keys and gamepad controls from .bindings.json
// (format: docs/content.md). Gamepads are applied once a frame (applyGamepads).
class InputActions {
 public:
  // Merges every action in `json["actions"]`, replacing those actions'
  // previous bindings. Unknown control names are logged and skipped.
  void loadBindings(const nlohmann::json& json, std::string_view source);

  // Adds one control to an action; false if the control name is unknown.
  bool bind(const std::string& action, std::string_view control);
  void unbind(const std::string& action);

  bool down(const std::string& action, const InputsManager& keys) const;
  bool pressed(const std::string& action, const InputsManager& keys) const;
  bool released(const std::string& action, const InputsManager& keys) const;
  // Strongest bound control, 0..1 (keys and buttons are 0 or 1).
  float value(const std::string& action, const InputsManager& keys) const;
  // True when pressed, then every `interval` seconds once held for `delay`
  // (menu and grid movement).
  bool repeated(const std::string& action, const InputsManager& keys, float delay, float interval) const;

  // One gamepad's raw state, as GLFW's gamepad mapping reports it: buttons in
  // inputs::Pad order (A..DPadLeft), axes left x, left y, right x, right y
  // (-1..1, y down), left trigger, right trigger (resting at -1).
  struct GamepadReading {
    std::array<bool, 15> buttons{};
    std::array<float, 6> axes{0, 0, 0, 0, -1, -1};
  };

  // Merges `pads` into one virtual pad: sticks past a deadzone become 0..1
  // directions, triggers 0..1, and anything at half or more is down, with
  // pressed/released edges since the last call. `dt` times how long controls are held.
  void applyGamepads(std::span<const GamepadReading> pads, float dt);
  bool gamepadConnected() const { return _padConnected; }

 private:
  static constexpr size_t kPadCount = static_cast<size_t>(inputs::Pad::Count);
  struct PadState {
    std::array<float, kPadCount> value{};
    std::array<bool, kPadCount> down{};
    std::array<bool, kPadCount> pressed{};
    std::array<bool, kPadCount> released{};
    std::array<float, kPadCount> held{};  // seconds
  };

  template <typename KeyPred, typename PadPred>
  bool any(const std::string& action, KeyPred keyPred, PadPred padPred) const;

  std::unordered_map<std::string, std::vector<inputs::Control>> _actions;
  PadState _pad;
  float _padDt = 0.0f;
  bool _padConnected = false;
};
