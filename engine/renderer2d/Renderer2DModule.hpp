#pragma once

#include "../core/app/EngineModule.hpp"
#include "../core/assets/AssetRegistry.hpp"
#include "../core/events/EventBus.hpp"
#include "Renderer2D.hpp"
#include "TextureHandle.hpp"

class Engine;

class Renderer2DModule : public EngineModule {
 public:
  ~Renderer2DModule() = default;

  void initialize(Engine& app) override;
  void shutdown(Engine& app) override;

  void tickMainThread(Engine& app, float dt) override;

  const char* name() const override { return "Renderer2DModule"; }

 private:
  Renderer2D _renderer;

  // Decoded textures keyed by the same AssetHandle the AssetManager issued for
  // the raw image bytes. The converter populates this; SpriteComponent's JSON
  // deserializer resolves texName → loadAsset(name) → registry.get(handle).
  AssetRegistry<TextureHandle> _textures;

  EventBus::EventHandle _tResize;
};
