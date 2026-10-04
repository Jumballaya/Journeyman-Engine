#pragma once

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

template <typename T>
void registerForTest(World& world) {
  world.registerComponent<T>();
}
