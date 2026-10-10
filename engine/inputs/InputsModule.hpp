#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "../core/app/Engine.hpp"
#include "../core/app/EngineModule.hpp"
#include <functional>
#include <map>

#include "InputActions.hpp"
#include "RemoteInput.hpp"
#include "Replay.hpp"

class InputsModule : public EngineModule {
 public:
  void bindScriptApi(Engine& app) override;
  void initialize(Engine& app) override;
  void shutdown(Engine& app) override;

  void tickMainThread(Engine& app, float dt) override;
  // The driver's "down|up|press <Key>" (JM_DRIVE): applied before the next
  // frame, like a device; written to JM_DRIVE_RECORD as replay lines.
  bool driveCommand(Engine& app, std::string_view verb, std::string_view args, nlohmann::json& reply) override;

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
  // Scripted key presses for automated runs (JM_INPUT_REPLAY=<file>), which
  // then replace the devices entirely. Each line: "<frame> down|up <KeyName>";
  // '#' starts a comment.
  // A key going down or up, recorded at `frame` when the session is (and F8
  // down drops a marker).
  void setKey(Engine& app, inputs::Key key, bool down, uint64_t frame);
  void loadReplay(const std::filesystem::path& path);
  void applyReplay(Engine& app);

  InputsManager _inputsManager;
  InputActions _actions;
  std::filesystem::path _replayFile;
  std::vector<inputs::ReplayEvent> _replay;
  size_t _replayCursor = 0;
  uint64_t _frame = 0;
  bool _driven = false;
  std::ofstream _record;  // JM_DRIVE_RECORD
  std::map<int32_t, RemoteInput> _remote;
  RemoteInput _idle;  // a remote player we've heard nothing from: nothing pressed
  std::function<int32_t(EntityId)> _controllerOf;
  // The remote input the calling script's entity reads, or null for local devices.
  const RemoteInput* remoteFor(EntityId entity) const;
};
