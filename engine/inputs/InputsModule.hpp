#pragma once

#include <cstdint>
#include <vector>

#include "../core/app/Engine.hpp"
#include "../core/app/EngineModule.hpp"
#include "InputActions.hpp"
#include "InputsManager.hpp"

class InputsModule : public EngineModule {
 public:
  InputsModule() = default;
  ~InputsModule() = default;

  void initialize(Engine& app) override;
  void shutdown(Engine& app) override;

  void tickMainThread(Engine& app, float dt) override;

  InputsManager& getManager() { return _inputsManager; }
  InputActions& getActions() { return _actions; }

  const char* name() const override { return "InputsModule"; }

 private:
  // Scripted key presses for automated runs (JM_INPUT_REPLAY=<file>). Each
  // line: "<frame> down|up <KeyName>"; '#' starts a comment.
  struct ReplayEvent {
    uint64_t frame;
    bool down;
    inputs::Key key;
  };
  void loadReplay(const char* path);
  void applyReplay();

  InputsManager _inputsManager;
  InputActions _actions;
  std::vector<ReplayEvent> _replay;
  size_t _replayCursor = 0;
  uint64_t _frame = 0;
};
