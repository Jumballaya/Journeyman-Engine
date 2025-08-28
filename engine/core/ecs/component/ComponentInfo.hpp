#pragma once

#include <cstddef>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>

#include "../entity/EntityId.hpp"
#include "ComponentId.hpp"

class World;

using JSONDeserializer = std::function<void(World&, EntityId, const nlohmann::json&)>;
using JSONSerializer = std::function<bool(const World&, EntityId, nlohmann::json&)>;

struct ComponentInfo {
  std::string name;
  size_t size;
  ComponentId id;
  JSONDeserializer deserializeFn;
  JSONSerializer serializeFn;

  explicit operator bool() const {
    return !name.empty();
  }
};