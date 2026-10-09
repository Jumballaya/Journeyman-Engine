#include "../Devices.hpp"

// No keyboard or gamepads (the server build): players' inputs arrive over the network.
namespace inputs::devices {

Key keyFromEvent(int, int) { return Key::Key_Invalid; }
std::vector<InputActions::GamepadReading> readGamepads() { return {}; }

}  // namespace inputs::devices
