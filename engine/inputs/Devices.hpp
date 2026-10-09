#pragma once

#include <vector>

#include "InputActions.hpp"
#include "InputsManager.hpp"

// The machine's keyboard and gamepads. The game build links the GLFW
// backend (devices/GlfwDevices.cpp); a server build links devices/NoDevices.cpp,
// whose games only see the inputs players send over the network.
namespace inputs::devices {

// The engine key for a window's key event (GLFW's scancode and key code), by
// physical position. Key_Invalid if it has none. Called only once a window
// exists (its key maps are built on first use, from the initialized GLFW).
Key keyFromEvent(int scancode, int platformKey);
// Every connected gamepad's state.
std::vector<InputActions::GamepadReading> readGamepads();

}  // namespace inputs::devices
