#pragma once

#include "../core/app/EngineModule.hpp"
#include "../core/events/EventBus.hpp"
#include "Renderer2D.hpp"

class Application;

class Renderer2DModule : public EngineModule {
 public:
  ~Renderer2DModule() = default;

  void initialize(Application& app);
  void shutdown(Application& app);

  void tickMainThread(Application& app, float dt);

 private:
  Renderer2D _renderer;

  EventBus::Token _tResize;
};