#pragma once

#include <functional>
#include <new>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

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
    static_assert(std::is_default_constructible_v<T>, "Components must be default-constructible");
    static_assert(std::is_move_constructible_v<T>, "Components must be move-constructible");

    ComponentId id = Component<T>::typeId();

    if (_components.find(id) != _components.end()) {
      return;
    }

    size_t bitIndex = _nextBitIndex++;
    _components.emplace(
        id,
        ComponentInfo{
            std::string(name),
            sizeof(T),
            sizeof(P),
            id,
            std::move(jsonDeserializer),
            std::move(jsonSerializer),
            std::move(podDeserializer),
            std::move(podSerializer),
            alignof(T),
            bitIndex,
            [](void* p) { new (p) T(); },
            [](void* p) { static_cast<T*>(p)->~T(); },
            [](void* dst, void* src) { new (dst) T(std::move(*static_cast<T*>(src))); },
            [](void* dst, const void* src) { new (dst) T(*static_cast<const T*>(src)); }});
    _nameToId[std::string(name)] = id;
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
  size_t _nextBitIndex = 0;
};
