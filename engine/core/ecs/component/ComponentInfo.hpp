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
  size_t podSize;
  ComponentId id;
  JSONDeserializer jsonDeserialize;
  JSONSerializer jsonSerialize;
  PODDeserializer podDeserialize;
  PODSerializer podSerialize;

  size_t alignment;
  size_t bitIndex;
  void (*defaultConstruct)(void* dst);
  void (*destruct)(void* dst);
  void (*moveConstruct)(void* dst, void* src);
  void (*copyConstruct)(void* dst, const void* src);

  // Fires once per entity, when the entity itself is being destroyed via
  // World::destroyEntity. Does NOT fire on archetype migrations (add/remove
  // component). Used for components that hold external resources (script
  // instances, audio sources) which must be released alongside the entity.
  void (*onDestroy)(void* componentPtr) = nullptr;

  explicit operator bool() const {
    return !name.empty();
  }
};
