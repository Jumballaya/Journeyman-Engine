#pragma once

#include <nlohmann/json.hpp>
#include <string>

#include "../ecs/entity/EntityId.hpp"

// What a running script is: the entity it belongs to and its authored params.
// Host functions reach it through host::ScriptCall.
struct ScriptInstanceContext {
  EntityId eid;
  std::string script;  // the script's asset path, for logs
  nlohmann::json params = nlohmann::json::object();
};
