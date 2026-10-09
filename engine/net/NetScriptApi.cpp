// The Net script API (cli/internal/stdlib/runtime/net.ts).

#include <cstring>

#include "../core/app/Engine.hpp"
#include "NetModule.hpp"

using host::ScriptCall;
using host::WasmBytes;

namespace {

// Writes i32s while they fit; returns how many there are.
int32_t writeIds(const std::vector<int32_t>& ids, WasmBytes out) {
  for (size_t i = 0; i < ids.size() && (i + 1) * 4 <= out.size; ++i) std::memcpy(out.data + i * 4, &ids[i], 4);
  return static_cast<int32_t>(ids.size());
}

}  // namespace

void NetModule::bindScriptApi(Engine& app) {
  ScriptManager& s = app.getScriptManager();
  _app = &app;

  // ---- The session ---------------------------------------------------------------
  s.bind("__jmNetRole", [this]() { return static_cast<int32_t>(_role); });
  s.bind("__jmNetTopology", [this]() { return static_cast<int32_t>(_topology); });
  s.bind("__jmNetStatus", [this]() { return static_cast<int32_t>(_status); });
  s.bind("__jmNetError", [this]() -> std::optional<std::string> { return _error; });
  s.bind("__jmNetLocalPlayer", [this]() { return _me; });
  s.bind("__jmNetHostPlayer", [this]() { return _hostPlayer; });
  s.bind("__jmNetPort", [this]() { return static_cast<int32_t>(localPort()); });
  s.bind("__jmNetHost", [this](int32_t port, int32_t topology) {
    return host(static_cast<uint16_t>(std::clamp(port, 0, 65535)), static_cast<Topology>(std::clamp(topology, 0, 2)));
  });
  s.bind("__jmNetJoin", [this](std::string address) { return join(address); });
  s.bind("__jmNetLeave", [this]() { leave(); });
  s.bind("__jmNetSetName", [this](std::string name) { setLocalName(std::move(name)); });
  s.bind("__jmNetPunch", [this](std::string address) { punch(address); });

  // ---- Players ---------------------------------------------------------------------
  s.bind("__jmNetPlayers", [this](WasmBytes out) { return writeIds(players(), out); });
  s.bind("__jmNetJoined", [this](WasmBytes out) { return writeIds(_joined, out); });
  s.bind("__jmNetLeft", [this](WasmBytes out) { return writeIds(_left, out); });
  s.bind("__jmNetPlayerName", [this](int32_t player) { return playerName(player); });
  s.bind("__jmNetPlayerAddress", [this](int32_t player) { return playerAddress(player); });
  s.bind("__jmNetPing", [this](int32_t player) { return ping(player); });

  // ---- Shared entities -------------------------------------------------------------
  s.bind("__jmNetSpawnPlayer", [this](int32_t player, std::string prefab, float x, float y, std::string overrides) {
    nlohmann::json json = overrides.empty() ? nlohmann::json::object() : nlohmann::json::parse(overrides, nullptr, false);
    if (!json.is_object()) json = nlohmann::json::object();
    return spawnFor(player, prefab, x, y, std::move(json));
  });
  s.bind("__jmNetIsMine", [this](EntityId id) { return simulatesHere(id); });
  s.bind("__jmNetIsShared", [this](EntityId id) { return netOf(id) != nullptr; });
  s.bind("__jmNetOwner", [this](EntityId id) {
    const NetworkComponent* n = netOf(id);
    return n ? n->owner : NetworkComponent::kHost;
  });
  s.bind("__jmNetController", [this](EntityId id) {
    const NetworkComponent* n = netOf(id);
    return n ? n->controller : NetworkComponent::kNobody;
  });

  // ---- Messages -----------------------------------------------------------------
  // entity.send: a shared entity's script gets it where it's simulated (this
  // replaces the engine's local-only binding).
  s.bind("__jmEntitySend", [this](ScriptCall& call, EntityId to, std::string name, std::string text, double number) {
    sendToEntity(call.self(), to, name, text, number, false);
  });
  s.bind("__jmNetSendEntity",
         [this](ScriptCall& call, EntityId to, std::string name, std::string text, double number, bool everywhere) {
           sendToEntity(call.self(), to, name, text, number, everywhere);
         });
  s.bind("__jmNetSendPlayer", [this](int32_t player, std::string name, std::string text, double number) {
    sendToPlayer(player, name, text, number);
  });
  s.bind("__jmNetInboxCount", [this]() { return static_cast<int32_t>(_inbox.size()); });
  auto at = [this](int32_t i) -> const Inbound* {
    return i >= 0 && static_cast<size_t>(i) < _inbox.size() ? &_inbox[static_cast<size_t>(i)] : nullptr;
  };
  s.bind("__jmNetInboxFrom", [at](int32_t i) { return at(i) ? at(i)->from : kHostPlayer; });
  s.bind("__jmNetInboxName", [at](int32_t i) -> std::optional<std::string> {
    return at(i) ? std::optional(at(i)->name) : std::nullopt;
  });
  s.bind("__jmNetInboxText", [at](int32_t i) -> std::optional<std::string> {
    return at(i) ? std::optional(at(i)->text) : std::nullopt;
  });
  s.bind("__jmNetInboxNumber", [at](int32_t i) { return at(i) ? at(i)->number : 0.0; });
}
