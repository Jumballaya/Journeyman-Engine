#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>
#include <unordered_map>

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

    auto [it, inserted] = _components.try_emplace(
        id,
        ComponentInfo{
            std::string(name),
            sizeof(T),
            sizeof(P),
            id,
            std::move(jsonDeserializer),
            std::move(jsonSerializer),
            std::move(podDeserializer),
            std::move(podSerializer)});
    if (inserted) {
      _nameToId[std::string(name)] = id;
    }
  }

  void forEachRegisteredComponent(auto&& fn) {
    for (auto& [id, _] : _components) fn(id);
  }

  const ComponentInfo* getInfo(ComponentId id) const {
    auto it = _components.find(id);
    if (it == _components.end()) return nullptr;
    return &it->second;
  }

  std::optional<ComponentId> getComponentIdByName(std::string_view name) const {
    auto it = _nameToId.find(std::string(name));
    if (it == _nameToId.end()) {
      return std::nullopt;
    }
    return it->second;
  }

 private:
  std::unordered_map<ComponentId, ComponentInfo> _components;
  std::unordered_map<std::string, ComponentId> _nameToId;
};