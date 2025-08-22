#pragma once
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <variant>

// Engine Events

namespace events {
struct WindowResized {
  int width{}, height{};
};
struct Quit {};

// Dynamic events to/from scripts
struct DynamicEvent {
  uint32_t id{};
  std::string name;
  nlohmann::json data;
};
}  // namespace events

using Event = std::variant<events::WindowResized, events::Quit, events::DynamicEvent>;