#pragma once

#include "../core/app/EngineModule.hpp"

class Application;

class Physics2DModule : public EngineModule {
 public:
  ~Physics2DModule() override = default;

  void initialize(Application& app) override;
  void shutdown(Application& app) override;
};