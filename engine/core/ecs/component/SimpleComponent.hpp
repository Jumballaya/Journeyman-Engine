#pragma once

#include <cstring>
#include <nlohmann/json.hpp>
#include <span>

#include "../World.hpp"

// Registers a component whose (de)serialization is plain field copying, given
// four small field-mapping functions instead of the four full World-level
// adapters registerComponent expects:
//
//   fromJson(T&, const json&)   apply authored fields onto a default T
//   toJson(const T&, json&)
//   fromPod(T&, const P&)       apply a script-side POD write
//   toPod(const T&) -> P
//
// JSON deserialization starts from a default-constructed T, so fromJson only
// needs to read the keys that are present.
template <ComponentType T, ComponentPodType P, typename FromJson, typename ToJson,
          typename FromPod, typename ToPod>
void registerSimpleComponent(World& world, FromJson fromJson, ToJson toJson,
                             FromPod fromPod, ToPod toPod,
                             void (*onDestroy)(void*) = nullptr) {
  world.registerComponent<T, P>(
      [fromJson](World& w, EntityId id, const nlohmann::json& json) {
        T comp{};
        fromJson(comp, json);
        w.addComponent<T>(id, std::move(comp));
      },
      [toJson](const World& w, EntityId id, nlohmann::json& out) {
        const T* comp = w.getComponent<T>(id);
        if (!comp) return false;
        toJson(*comp, out);
        return true;
      },
      [fromPod](World& w, EntityId id, std::span<const std::byte> in) {
        T* comp = w.getComponent<T>(id);
        if (!comp || in.size() < sizeof(P)) return false;
        P pod{};
        std::memcpy(&pod, in.data(), sizeof(P));
        fromPod(*comp, pod);
        return true;
      },
      [toPod](const World& w, EntityId id, std::span<std::byte> out, size_t& written) {
        const T* comp = w.getComponent<T>(id);
        if (!comp || out.size() < sizeof(P)) return false;
        P pod = toPod(*comp);
        std::memcpy(out.data(), &pod, sizeof(P));
        written = sizeof(P);
        return true;
      },
      onDestroy);
}
