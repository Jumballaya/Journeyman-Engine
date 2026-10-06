#pragma once

#include <string_view>

#include "ComponentId.hpp"

#define COMPONENT_NAME(name) \
  static constexpr std::string_view typeName = name

template <typename T>
class Component {
 public:
  static ComponentId typeId() { return GetComponentId<T>(); }

  static constexpr std::string_view name() { return T::typeName; }
};