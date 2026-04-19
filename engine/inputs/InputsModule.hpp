#pragma once

#include "../core/app/Engine.hpp"
#include "../core/app/EngineModule.hpp"
#include "InputsManager.hpp"

class InputsModule : public EngineModule {
 public:
  InputsModule() = default;
  ~InputsModule() = default;

  void initialize(Engine& app) override;
  void shutdown(Engine& app) override;

  void tickMainThread(Engine& app, float dt) override;

  InputsManager& getManager() { return _inputsManager; }

  const char* name() const override { return "InputsModule"; }

 private:
  InputsManager _inputsManager;
};