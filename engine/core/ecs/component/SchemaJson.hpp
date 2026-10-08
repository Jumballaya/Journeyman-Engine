#pragma once

#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "ComponentRegistry.hpp"

// Every registered component as JSON, for tools (`journeyman_engine
// --schema`, jm, an MCP server): what each one's scene JSON holds, field by
// field, and which fields scripts can reach. Components are keyed by name:
//   {"schemaVersion": 1, "components": {"BoxColliderComponent": {
//      "label", "category", "summary",
//      "fields": [{"key", "kind", "default", "hint", ...}],
//      "scriptFields": [{"name", "type": "f32" | "u32"}]}}}
// A field has "min"/"max"/"step" when bounded, "choices", "assetTypes", or
// nested "fields" (a group) when it has them. With `hostFunctions` (name ->
// wasm signature, e.g. "v(iiffffii)"), also "hostFunctions": the script API's
// imports, which the runtime's env.ts declarations must match.
nlohmann::json schemaJson(const ComponentRegistry& registry,
                          const std::map<std::string, std::string>& hostFunctions = {});
