#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

#include "../ecs/entity/EntityId.hpp"

// A message from one script to another (entity.send in scripts).
struct ScriptMessage {
  EntityId from;
  std::string name;
  std::string text;
  double number = 0.0;
  // Multiplayer: the player whose machine sent it (-1: the host or server);
  // kLocal for a message from this machine.
  static constexpr int32_t kLocal = -2;
  int32_t player = kLocal;
};

// What a running script is: the entity it belongs to and its authored params.
// Host functions reach it through host::ScriptCall.
struct ScriptInstanceContext {
  EntityId eid;
  std::string script;  // the script's asset path, for logs
  nlohmann::json params = nlohmann::json::object();
  const ScriptMessage* message = nullptr;  // set while onMessage runs
};
