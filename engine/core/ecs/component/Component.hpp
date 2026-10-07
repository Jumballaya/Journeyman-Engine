#pragma once

#include <string_view>

#include "ComponentId.hpp"

#define COMPONENT_NAME(name) \
  [[maybe_unused]] static constexpr std::string_view typeName = name  // a component only used as a type

template <typename T>
class Component {
 public:
  static ComponentId typeId() { return GetComponentId<T>(); }

  static constexpr std::string_view name() { return T::typeName; }
};