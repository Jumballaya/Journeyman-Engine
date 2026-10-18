#pragma once

#include <cstdint>

#include "../core/app/Engine.hpp"
#include "../core/app/EngineModule.hpp"
#include <functional>
#include <map>

#include "InputActions.hpp"
#include "RemoteInput.hpp"

class InputsModule : public EngineModule {
 public:
  void bindScriptApi(Engine& app) override;
  void initialize(Engine& app) override;
  void shutdown(Engine& app) override;

  void step(Engine& app, float dt) override;
  void draw(Engine& app, float dt) override;

  // A key going down or up, as a device's does: recorded when the session is
  // (F8 down is the agent tools' moment, when they take it).
  void setKey(Engine& app, inputs::Key key, bool down);
  // Keys the agent tools press (a replay file, the driver: inputs/agent/):
  // called in every step, after the devices', where they press them.
  void onKeysDue(std::function<void()> keysDue) { _keysDue = std::move(keysDue); }

  InputsManager& getManager() { return _inputsManager; }
  InputActions& getActions() { return _actions; }

  // Multiplayer. This machine's input as a snapshot to send.
  InputSnapshot localSnapshot() const;
  // A remote player's input (made on first use); dropped when they leave.
  RemoteInput& remote(int32_t player) { return _remote[player]; }
  void dropRemote(int32_t player) { _remote.erase(player); }
  // Whose input an entity's script reads: a remote player's id, or -1 for
  // this machine's devices (the default for every entity).
  void setControllerResolver(std::function<int32_t(EntityId)> resolver) { _controllerOf = std::move(resolver); }

  const char* name() const override { return "InputsModule"; }

 private:
  // Every key and button up, as events (so a recording has them).
  void releaseAll(Engine& app);

  InputsManager _inputsManager;
  InputActions _actions;
  std::function<void()> _keysDue;
  bool _wasMuted = false;
  std::map<int32_t, RemoteInput> _remote;
  RemoteInput _idle;  // a remote player we've heard nothing from: nothing pressed
  std::function<int32_t(EntityId)> _controllerOf;
  // The remote input the calling script's entity reads, or null for local devices.
  const RemoteInput* remoteFor(EntityId entity) const;
};
