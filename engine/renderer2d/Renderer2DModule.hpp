#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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

  // Compiled custom shader for a .frag asset path, or an invalid handle if
  // the asset wasn't loaded (shaders compile at asset-load time, on the main
  // thread, so list them in the manifest's assets).
  ShaderHandle customShader(std::string_view path) const;
  PostEffectHandle addCustom(std::string_view shaderPath);

  // Screen shake: offsets the camera by up to `amplitude` world units,
  // decaying linearly over `duration` seconds. Strongest request wins.
  void shake(float amplitude, float duration);
  void setCameraPosition(glm::vec2 p);
  void setClearColor(glm::vec4 c) { _pendingClearColor = c; }

  // Overlay passes run every frame right before the renderer composites the
  // frame, so UI-style modules can submit screen-space quads in the same
  // frame (and get included in transitions and post-effects).
  void addOverlayPass(std::function<void(Renderer2D&)> pass) { _overlayPasses.push_back(std::move(pass)); }

  Renderer2D& renderer() { return _renderer; }
  TextureHandle textureFor(AssetHandle image) const;
  AtlasManager& atlases() { return _atlasManager; }

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

  EventBus::EventHandle _tResize = 0;

  // Built-in post-effect shaders are compiled once during initialize on the
  // main thread (GL context only exists there). Scripts call addBuiltin from
  // worker threads, so GL work during addBuiltin would race with the main
  // thread and segfault on macOS. Cached handles make addBuiltin a pure
  // lookup with no GL calls.
  std::array<ShaderHandle, static_cast<size_t>(BuiltinEffectId::Count)> _builtinShaders{};

  // Scene transition compositing state. Polled from SceneManager each
  // tickMainThread: rising edge captures the outgoing frame and pushes a
  // Crossfade effect; falling edge tears them down. SceneManager is
  // renderer-blind by design (engine_renderer_2d depends on engine_app, not
  // the other way around), so the integration lives here.
  bool _transitionLive = false;
  TextureHandle _transitionSnapshot{};

  std::unordered_map<std::string, ShaderHandle> _customShaders;  // canonical path → shader
  std::vector<std::function<void(Renderer2D&)>> _overlayPasses;

  // Camera shake + position; written by scripts (worker threads), applied in
  // tickMainThread. Scripts and the main thread never run concurrently.
  glm::vec2 _cameraBase{0.0f};
  float _shakeAmplitude = 0.0f;
  float _shakeRemaining = 0.0f;
  float _shakeDuration = 0.0f;
  std::optional<glm::vec4> _pendingClearColor;

  // JM_CAPTURE_DIR / JM_CAPTURE_FRAMES: write listed frames as PNGs.
  std::filesystem::path _captureDir;
  std::vector<uint64_t> _captureFrames;
  uint64_t _frame = 0;
  void captureIfRequested();
  void updateTransition(Engine& app);
};
