#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "../core/app/Engine.hpp"
#include "../core/app/EngineModule.hpp"
#include "InputActions.hpp"
#include "Replay.hpp"

class InputsModule : public EngineModule {
 public:
  void initialize(Engine& app) override;
  void shutdown(Engine& app) override;

  void tickMainThread(Engine& app, float dt) override;

  InputsManager& getManager() { return _inputsManager; }
  InputActions& getActions() { return _actions; }

  const char* name() const override { return "InputsModule"; }

 private:
  // Scripted key presses for automated runs (JM_INPUT_REPLAY=<file>), which
  // then replace the devices entirely. Each line: "<frame> down|up <KeyName>";
  // '#' starts a comment.
  void loadReplay(const std::filesystem::path& path);
  void bindScriptApi(ScriptManager& scripts);
  void applyReplay();
  bool replaying() const { return !_replayFile.empty(); }

  InputsManager _inputsManager;
  InputActions _actions;
  std::filesystem::path _replayFile;
  std::vector<inputs::ReplayEvent> _replay;
  size_t _replayCursor = 0;
  uint64_t _frame = 0;
};
