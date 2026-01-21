#pragma once

#include <cstddef>
#include <functional>
#include <nlohmann/json.hpp>
#include <string_view>

#include "../entity/EntityId.hpp"
#include "ComponentId.hpp"

class World;

struct ComponentInfo {
  std::string_view name;
  size_t size;
  ComponentId id;
  
  // Deserialization function (existing)
  std::function<void(World&, EntityId, const nlohmann::json&)> deserializeFn;
  
  // Serialization function (NEW)
  // Returns JSON representation of component, or null if component doesn't exist
  std::function<nlohmann::json(World&, EntityId)> serializeFn;
  
  // Whether component should be saved (default: true)
  bool saveable{true};

  explicit operator bool() const {
    return !name.empty();
  }
};