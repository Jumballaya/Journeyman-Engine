#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "InputsManager.hpp"

// One player's input as their machine reads it: which keys are down, and each
// bound action's state (their own bindings, gamepads included). Multiplayer
// sends it when it changes.
struct InputSnapshot {
  struct Action {
    std::string name;
    bool down = false;
    float value = 0.0f;
    bool operator==(const Action&) const = default;
  };
  std::array<uint8_t, (inputs::Key::Key_Count + 7) / 8> keys{};
  std::vector<Action> actions;  // sorted by name

  bool keyDown(inputs::Key key) const { return key < inputs::Key::Key_Count && (keys[key / 8] >> (key % 8)) & 1; }
  void setKey(inputs::Key key, bool down) {
    if (key >= inputs::Key::Key_Count) return;
    keys[key / 8] = static_cast<uint8_t>(down ? keys[key / 8] | (1 << (key % 8)) : keys[key / 8] & ~(1 << (key % 8)));
  }
  bool operator==(const InputSnapshot&) const = default;
};

// A remote player's input, from the snapshots they send, read the way local
// devices are: pressed and released for one frame, held times for repeats.
class RemoteInput {
 public:
  // A snapshot arrived: edges against the last one. Several in one frame
  // keep every press (a tap is pressed and released in the same frame).
  void apply(const InputSnapshot& snapshot);
  // Once a frame, before this frame's snapshots: clears last frame's edges.
  void tick(float dt);

  const InputsManager& keys() const { return _keys; }
  bool down(const std::string& action) const;
  bool pressed(const std::string& action) const;
  bool released(const std::string& action) const;
  float value(const std::string& action) const;
  bool repeated(const std::string& action, float delay, float interval) const;

 private:
  struct ActionState {
    bool down = false, pressed = false, released = false;
    float value = 0.0f, held = 0.0f;
  };
  const ActionState* find(const std::string& action) const;

  InputsManager _keys;
  InputSnapshot _last;
  std::map<std::string, ActionState, std::less<>> _actions;
  float _dt = 0.0f;
};
