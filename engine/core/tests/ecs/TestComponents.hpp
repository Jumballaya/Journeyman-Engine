#pragma once

#include <cstddef>
#include <nlohmann/json.hpp>
#include <span>

#include "World.hpp"
#include "component/Component.hpp"

struct Position : Component<Position> {
  COMPONENT_NAME("Position");
  float x = 0.0f;
  float y = 0.0f;
};

struct Velocity : Component<Velocity> {
  COMPONENT_NAME("Velocity");
  float dx = 0.0f;
  float dy = 0.0f;
};

struct Health : Component<Health> {
  COMPONENT_NAME("Health");
  int hp = 100;
};

// Register a component with no-op (de)serializers — sufficient for tests that
// exercise registry lookup or cloneEntity (which iterates the registry).
template <typename T>
void registerForTest(World& world) {
  world.registerComponent<T, T>(
      [](World&, EntityId, const nlohmann::json&) {},
      [](const World&, EntityId, nlohmann::json&) { return false; },
      [](World&, EntityId, std::span<const std::byte>) { return false; },
      [](const World&, EntityId, std::span<std::byte>, size_t&) { return false; });
}
