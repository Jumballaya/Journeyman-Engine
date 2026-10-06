#pragma once

#include <map>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "Component.hpp"
#include "ComponentConcepts.hpp"
#include "ComponentId.hpp"
#include "ComponentInfo.hpp"

class ComponentRegistry {
public:
  // Registers T under T::name(). Re-registering is a no-op.
  template <ComponentType T>
  void registerComponent(ComponentInfo info) {
    static_assert(std::is_default_constructible_v<T>, "Components must be default-constructible");
    static_assert(std::is_move_constructible_v<T>, "Components must be move-constructible");
    // Archetype columns are plain heap byte arrays.
    static_assert(alignof(T) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__, "Components must not be over-aligned");

    const ComponentId id = Component<T>::typeId();
    if (_components.contains(id)) return;

    info.name = std::string(T::name());
    info.size = sizeof(T);
    info.id = id;
    info.bitIndex = _nextBitIndex++;
    info.defaultConstruct = [](void *p) { new (p) T(); };
    info.destruct = [](void *p) { static_cast<T *>(p)->~T(); };
    info.moveConstruct = [](void *dst, void *src) { new (dst) T(std::move(*static_cast<T *>(src))); };
    info.copyConstruct = [](void *dst, const void *src) { new (dst) T(*static_cast<const T *>(src)); };
    _nameToId[info.name] = id;
    _components.emplace(id, std::move(info));
  }

  void forEachRegisteredComponent(auto &&fn) const {
    for (auto &[id, _] : _components) fn(id);
  }

  const ComponentInfo *getInfo(ComponentId id) const {
    auto it = _components.find(id);
    return it == _components.end() ? nullptr : &it->second;
  }

  std::optional<ComponentId> getComponentIdByName(std::string_view name) const {
    auto it = _nameToId.find(name);
    if (it == _nameToId.end()) return std::nullopt;
    return it->second;
  }

  const ComponentInfo *getInfoByName(std::string_view name) const {
    auto id = getComponentIdByName(name);
    return id ? getInfo(*id) : nullptr;
  }

private:
  std::unordered_map<ComponentId, ComponentInfo> _components;
  std::map<std::string, ComponentId, std::less<>> _nameToId;
  size_t _nextBitIndex = 0;
};
