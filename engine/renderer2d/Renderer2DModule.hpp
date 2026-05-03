#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "../core/app/EngineModule.hpp"
#include "../core/assets/AssetRegistry.hpp"
#include "../core/events/EventBus.hpp"
#include "AtlasManager.hpp"
#include "Renderer2D.hpp"
#include "ShaderHandle.hpp"
#include "TextureHandle.hpp"
#include "posteffects/PostEffect.hpp"
#include "posteffects/PostEffectHandle.hpp"
#include "posteffects/builtins.hpp"

class Engine;

class Renderer2DModule : public EngineModule {
 public:
  ~Renderer2DModule() = default;

  void initialize(Engine& app) override;
  void shutdown(Engine& app) override;

  void tickMainThread(Engine& app, float dt) override;

  const char* name() const override { return "Renderer2DModule"; }

  PostEffectHandle addEffect(PostEffect effect);
  PostEffectHandle addBuiltin(BuiltinEffectId id);
  void removeEffect(PostEffectHandle handle);
  void setEffectEnabled(PostEffectHandle handle, bool enabled);
  void setEffectUniform(PostEffectHandle handle, std::string_view name, UniformValue value);
  void setEffectAuxTexture(PostEffectHandle handle, TextureHandle tex);
  void moveEffect(PostEffectHandle handle, size_t newIndex);
  size_t effectCount() const;

  // Snapshot the current scene surface into a standalone texture for use as a
  // transition aux texture. Both calls are main-thread-only; they touch GL
  // resources directly. SceneManager's renderer-side polling in
  // tickMainThread is the intended caller.
  TextureHandle captureSceneFrame();
  void releaseCapturedTexture(TextureHandle handle);

 private:
  Renderer2D _renderer;

  // Decoded textures keyed by the same AssetHandle the AssetManager issued for
  // the raw image bytes. The converter populates this; SpriteComponent's JSON
  // deserializer resolves texName → loadAsset(name) → registry.get(handle).
  AssetRegistry<TextureHandle> _textures;

  // Loaded atlases keyed by the AssetHandle the AssetManager issued for the
  // .atlas.json. Populated by the atlas converter; consumed by SpriteComponent's
  // texture#region deserializer path (F.3).
  AtlasManager _atlasManager;

  EventBus::EventHandle _tResize;

  // Built-in post-effect shaders are compiled once during initialize on the
  // main thread (GL context only exists there). Scripts call addBuiltin from
  // worker threads, so GL work during addBuiltin would race with the main
  // thread and segfault on macOS. Cached handles make addBuiltin a pure
  // lookup with no GL calls.
  std::array<ShaderHandle, 6> _builtinShaders{};

  // Scene transition compositing state. Polled from SceneManager each
  // tickMainThread: rising edge captures the outgoing frame and pushes a
  // Crossfade effect; falling edge tears them down. SceneManager is
  // renderer-blind by design (engine_renderer_2d depends on engine_app, not
  // the other way around), so the integration lives here.
  bool _transitionLive = false;
  TextureHandle _transitionSnapshot{};
  PostEffectHandle _transitionEffect{};
};
