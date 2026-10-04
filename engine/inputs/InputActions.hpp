#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
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
std::string_view keyName(Key key);

}  // namespace inputs

// Named actions ("fire") bound to keys and gamepad controls from .bindings.json
// (format: docs/content.md). Gamepads poll on the main thread; queries are thread-safe.
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

  // Merges all connected gamepads into one virtual pad and computes edges.
  void pollGamepads();
  bool gamepadConnected() const { return _padConnected; }

 private:
  struct PadState {
    std::array<float, static_cast<size_t>(inputs::Pad::Count)> value{};
    std::array<bool, static_cast<size_t>(inputs::Pad::Count)> down{};
    std::array<bool, static_cast<size_t>(inputs::Pad::Count)> pressed{};
    std::array<bool, static_cast<size_t>(inputs::Pad::Count)> released{};
  };

  template <typename KeyPred, typename PadPred>
  bool any(const std::string& action, KeyPred keyPred, PadPred padPred) const;

  mutable std::mutex _mutex;  // guards _actions (scripts may rebind)
  std::unordered_map<std::string, std::vector<inputs::Control>> _actions;
  PadState _pad;
  bool _padConnected = false;
};
