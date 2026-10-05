#pragma once

#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "../core/app/EngineModule.hpp"
#include "../core/assets/AssetRegistry.hpp"
#include "../core/ecs/entity/EntityId.hpp"
#include "AtlasManager.hpp"
#include "Renderer2D.hpp"

class Engine;
class World;

// 2D rendering: images, atlases and .frag shaders; sprites and flipbooks; the
// post-effect chain and transitions; effects, camera and animation for scripts.
class Renderer2DModule : public EngineModule {
 public:
  void initialize(Engine& app) override;
  void shutdown(Engine& app) override;
  void tickMainThread(Engine& app, float dt) override;
  const char* name() const override { return "Renderer2DModule"; }

  // A drawable image: a whole texture or an atlas region.
  struct Image {
    TextureHandle texture;
    glm::vec4 texRect{0.0f, 0.0f, 1.0f, 1.0f};
    glm::vec2 size{0.0f};  // pixels
  };
  // Resolves "path/to/image.png" or "path/to/atlas.json#region", loading the
  // asset on first use. Main thread only.
  std::optional<Image> resolveImage(const std::string& reference);

  // Runs every frame just before the frame is composited, so overlay modules
  // (UI) submit screen quads that transitions and effects also apply to.
  void addOverlayPass(std::function<void(Renderer2D&)> pass) { _overlayPasses.push_back(std::move(pass)); }

  // An editor's camera: replaces the game camera and logical size until
  // cleared; overlays then skip screen-space UI (editorView()). Main thread.
  struct EditorView {
    glm::vec2 center{0.0f};
    float zoom = 1.0f;
    glm::ivec2 logicalSize{1, 1};
  };
  void setEditorView(std::optional<EditorView> view);
  bool editorView() const { return _editorView.has_value(); }

  Renderer2D& renderer() { return _renderer; }
  AtlasManager& atlases() { return _atlases; }

 private:
  Engine* _app = nullptr;
  Renderer2D _renderer;
  AssetRegistry<TextureHandle> _images;  // keyed by the image asset's handle
  AtlasManager _atlases;
  std::unordered_map<std::string, ShaderHandle> _shaders;  // .frag path → program
  std::vector<std::function<void(Renderer2D&)>> _overlayPasses;

  // Written by scripts (worker threads), applied in tickMainThread; the two
  // never overlap.
  glm::vec2 _cameraBase{0.0f};
  float _shakeAmplitude = 0.0f, _shakeDuration = 0.0f, _shakeRemaining = 0.0f;
  std::optional<glm::vec4> _pendingClearColor;
  std::mutex _textureMutex;
  std::vector<std::pair<EntityId, std::string>> _pendingTextures;  // resolved on the main thread

  uint64_t _frame = 0;
  std::optional<EditorView> _editorView;

  void registerAssetTypes(Engine& app);
  void registerComponents(Engine& app);
  void bindScriptApi(Engine& app);
  void captureIfRequested(const Engine& app);
  void applyPendingTextures(World& world);
  // A loaded .frag by path or short name ("crt").
  ShaderHandle shaderFor(std::string_view nameOrPath) const;
};
