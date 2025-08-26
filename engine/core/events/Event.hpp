#pragma once
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <variant>

namespace events {

// Engine Events
struct WindowResized {
  int width{}, height{};
};

struct Quit {};

struct KeyDown {
  char key;
};
struct KeyUp {
  char key;
};
struct KeyRepeat {
  char key;
};

// Dynamic events to/from scripts
struct DynamicEvent {
  uint32_t id{};
  std::string name;
  nlohmann::json data;
};
}  // namespace events

using Event = std::variant<events::WindowResized, events::Quit, events::KeyDown, events::DynamicEvent>;