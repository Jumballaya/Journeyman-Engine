#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Component.hpp"
#include "ComponentConcepts.hpp"
#include "ComponentId.hpp"
#include "ComponentInfo.hpp"

class World;

class ComponentRegistry {
 public:
  template <ComponentType T, ComponentPodType P>
  void registerComponent(
      std::string_view name,
      JSONDeserializer jsonDeserializer,
      JSONSerializer jsonSerializer,
      PODDeserializer podDeserializer,
      PODSerializer podSerializer) {
    ComponentId id = Component<T>::typeId();

    if (_components.size() <= id) {
      _components.resize(id + 1);
      _components[id] = ComponentInfo{
          std::string(name),
          sizeof(T),
          sizeof(P),
          id,
          std::move(jsonDeserializer),
          std::move(jsonSerializer),
          std::move(podDeserializer),
          std::move(podSerializer)};
      _nameToId[std::string(name)] = id;
    }
  }

  void forEachRegisteredComponent(auto&& fn) {
    for (ComponentId id = 0; id < _components.size(); ++id) {
      if (_components[id]) fn(id);
    }
  }

  const ComponentInfo* getInfo(ComponentId id) const {
    if (id >= _components.size()) return nullptr;
    return &_components[id];
  }

  std::optional<ComponentId> getComponentIdByName(std::string_view name) const {
    auto it = _nameToId.find(std::string(name));
    if (it == _nameToId.end()) {
      return std::nullopt;
    }
    return it->second;
  }

 private:
  std::vector<ComponentInfo> _components;
  std::unordered_map<std::string, ComponentId> _nameToId;
};