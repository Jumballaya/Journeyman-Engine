#pragma once

#include <string>
#include <unordered_map>

#include "../assets/AssetHandle.hpp"
#include "../core/app/EngineModule.hpp"
#include "../core/events/EventBus.hpp"
#include "Renderer2D.hpp"
#include "TextureHandle.hpp"

class Application;

class Renderer2DModule : public EngineModule {
 public:
  ~Renderer2DModule() = default;

  void initialize(Application& app) override;
  void shutdown(Application& app) override;

  void tickMainThread(Application& app, float dt) override;

  const char* name() const override { return "Renderer2DModule"; }

 private:
  Renderer2D _renderer;

  std::unordered_map<std::string, TextureHandle> _textureMap;

  EventBus::EventHandle _tResize;
};