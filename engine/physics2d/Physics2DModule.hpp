#pragma once

#include "../core/app/EngineModule.hpp"

class Engine;

class Physics2DModule : public EngineModule {
 public:
  ~Physics2DModule() override = default;

  void initialize(Engine& app) override;
  void shutdown(Engine& app) override;
};