#pragma once

#include "./component/ComponentRegistry.hpp"
#include "./system/SystemRegistry.hpp"

class ECSRegistry {
 public:
  ECSRegistry() = default;
  ~ECSRegistry() = default;

  const ComponentRegistry& getComponentRegistry() const {
    return _componentRegistry;
  }

  ComponentRegistry& getComponentRegistry() {
    return _componentRegistry;
  }

 private:
  ComponentRegistry _componentRegistry;
  SystemRegistry _systemRegistry;
};