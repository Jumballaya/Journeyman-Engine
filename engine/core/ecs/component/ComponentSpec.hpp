#pragma once

#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "../entity/EntityId.hpp"
#include "ComponentConcepts.hpp"
#include "ComponentInfo.hpp"

// Optional behavior beyond storage: fromJson (scene/prefab data), scriptFields
// (what scripts may touch), onDestroy (release external resources) and schema
// (how an editor shows the JSON fromJson reads).
template <ComponentType T>
struct ComponentSpec {
  std::function<void(T&, const nlohmann::json&, EntityId)> fromJson;
  std::vector<ScriptField> scriptFields;
  std::function<void(T&)> onDestroy;
  ComponentSchema schema;
};

// A script-visible field: `access` returns a reference to a float or uint32
// inside the component, e.g. scriptField<Transform>("x", [](Transform& t) -> float& { return t.position.x; }).
template <ComponentType T, typename Access>
ScriptField scriptField(std::string name, Access access) {
  using Ref = std::invoke_result_t<Access, T&>;
  static_assert(std::is_lvalue_reference_v<Ref> && sizeof(std::remove_reference_t<Ref>) == 4,
                "script fields must be 4-byte lvalues (float or uint32_t)");
  return ScriptField{std::move(name), [access](void* c) -> void* { return &access(*static_cast<T*>(c)); },
                     std::is_integral_v<std::remove_reference_t<Ref>>};
}
