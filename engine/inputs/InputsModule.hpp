#pragma once

#include "../core/app/Application.hpp"
#include "../core/app/EngineModule.hpp"
#include "InputsManager.hpp"

class InputsModule : public EngineModule {
 public:
  InputsModule() = default;
  ~InputsModule() = default;

  void initialize(Application& app) override;
  void shutdown(Application& app) override;

  void tickMainThread(Application& app, float dt) override;

  InputsManager& getManager() { return _inputsManager; }

  const char* name() const override { return "InputsModule"; }

 private:
  InputsManager _inputsManager;
};