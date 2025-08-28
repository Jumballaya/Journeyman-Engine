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

using PODDeserializer = std::function<bool(World&, EntityId, std::span<const std::byte> /*in*/)>;
using PODSerializer = std::function<bool(const World&, EntityId, std::span<std::byte> /*out*/, size_t& /*written*/)>;

struct ComponentInfo {
  std::string name;
  size_t size;
  ComponentId id;
  JSONDeserializer jsonDeserialize;
  JSONSerializer jsonSerialize;
  PODDeserializer podDeserialize;
  PODSerializer podSerialize;

  explicit operator bool() const {
    return !name.empty();
  }
};