#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "../core/app/Engine.hpp"
#include "../core/app/EngineModule.hpp"
#include "InputActions.hpp"
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

  const char* name() const override { return "InputsModule"; }

 private:
  // Scripted key presses for automated runs (JM_INPUT_REPLAY=<file>), which
  // then replace the devices entirely. Each line: "<frame> down|up <KeyName>";
  // '#' starts a comment.
  void loadReplay(const std::filesystem::path& path);
  void applyReplay();
  // Replayed or driven: the real devices are ignored.
  bool replaying() const { return !_replayFile.empty() || _driven; }

  InputsManager _inputsManager;
  InputActions _actions;
  std::filesystem::path _replayFile;
  std::vector<inputs::ReplayEvent> _replay;
  size_t _replayCursor = 0;
  uint64_t _frame = 0;
  bool _driven = false;
  std::ofstream _record;  // JM_DRIVE_RECORD
};
