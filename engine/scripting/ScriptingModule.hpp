#pragma once

#include "../app/Application.hpp"
#include "../app/EngineModule.hpp"
#include "ScriptManager.hpp"

class ScriptingModule : public EngineModule {
 public:
  void initialize(Application& app) override;
  void shutdown(Application& app) override;

 private:
  ScriptManager _scriptManager;
};