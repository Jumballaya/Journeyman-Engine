#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "../entity/EntityId.hpp"
#include "ComponentId.hpp"
#include "FieldSchema.hpp"

class World;

// A 4-byte component field scripts may read and write (f32 or u32), located
// inside a component instance.
struct ScriptField {
  std::string name;
  std::function<void*(void* component)> locate;
  bool integer = false;  // uint32 (a mask); otherwise a float
  // State dumps leave this field out while the named field of the same
  // component is zero (a sprite's shadow fields while it has no shadow).
  std::string dumpedWith;
};

// Everything the ECS knows about a registered component type.
struct ComponentInfo {
  std::string name;
  size_t size;
  ComponentId id;
  size_t bitIndex;

  // Builds the component from scene/prefab JSON and adds it to the entity.
  std::function<void(World&, EntityId, const nlohmann::json&)> addFromJson;
  // Script-visible fields; scripts see them packed in this order, 4 bytes each.
  std::vector<ScriptField> scriptFields;
  // Runs when the owning entity is destroyed (not on archetype moves).
  std::function<void(void* component)> onDestroy;
  ComponentSchema schema;

  void (*defaultConstruct)(void* dst);
  void (*destruct)(void* dst);
  void (*moveConstruct)(void* dst, void* src);
  void (*copyConstruct)(void* dst, const void* src);

  explicit operator bool() const { return !name.empty(); }
};
