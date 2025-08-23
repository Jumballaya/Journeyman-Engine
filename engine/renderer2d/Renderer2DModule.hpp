#pragma once

#include "../app/EngineModule.hpp"
#include "Renderer2D.hpp"

class Application;

class Renderer2DModule : public EngineModule {
 public:
  void initialize(Application& app);
  void shutdown(Application& app);

  void tickMainThread(Application& app, float dt);

 private:
  Renderer2D _renderer;
};