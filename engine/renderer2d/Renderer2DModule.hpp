#pragma once

#include <functional>
#include <future>
#include <optional>
#include <random>
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
struct SpriteComponent;

// 2D rendering: images, atlases and .frag shaders; sprites and flipbooks; the
// post-effect chain and transitions; effects, camera and animation for scripts.
class Renderer2DModule : public EngineModule {
 public:
  void registerComponents(Engine& app) override;
  void bindScriptApi(Engine& app) override;
  void initialize(Engine& app) override;
  void shutdown(Engine& app) override;
  void tickMainThread(Engine& app, float dt) override;
  // state["draw"]: the last frame's draw list (world sprites back to front,
  // then screen quads), with each item's image, place, size, color and z.
  void describeState(Engine& app, nlohmann::json& state) override;
  // The driver's "capture <path>": the last frame as a PNG.
  bool driveCommand(Engine& app, std::string_view verb, std::string_view args, nlohmann::json& reply) override;
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
  // cleared; the game's UI then goes where uiPlacement() says. Main thread.
  struct EditorView {
    glm::vec2 center{0.0f};
    float zoom = 1.0f;
    glm::ivec2 logicalSize{1, 1};
    // Draw the game's screen-space UI inside its frame (the game's logical
    // size, centered on the world origin), laid out as in the game.
    bool showUi = false;
    glm::ivec2 gameSize{0};
  };
  void setEditorView(std::optional<EditorView> view);
  // Under an editor view: where the game's UI goes, and the size to lay it
  // out at; nullopt when UI is hidden there. Without one: identity and the logical size.
  struct UiPlacement {
    Renderer2D::ScreenTransform transform;
    glm::vec2 layoutSize;
  };
  std::optional<UiPlacement> uiPlacement() const;

  // Authoring (the editor): `source` compiled as a post effect drawn last,
  // replacing the one shown this way before; false with the compiler's
  // message in `error` (the previous one stays). Its uniforms set by name.
  bool showPostEffect(std::string_view source, std::string& error);
  void setPostEffectUniform(const std::string& name, UniformValue value);

  Renderer2D& renderer() { return _renderer; }
  AtlasManager& atlases() { return _atlases; }

 private:
  Engine* _app = nullptr;
  Renderer2D _renderer;
  AssetRegistry<TextureHandle> _images;  // keyed by the image asset's handle
  std::unordered_map<uint32_t, std::string> _imagePaths;  // texture id -> image path, for state dumps
  AtlasManager _atlases;
  std::unordered_map<std::string, ShaderHandle> _shaders;  // .frag path → program
  PostEffectHandle _authoredEffect{};  // shown by showPostEffect
  ShaderHandle _authoredShader{};
  TextureHandle _blackTexture{};
  std::vector<std::function<void(Renderer2D&)>> _overlayPasses;

  // Written by scripts, applied in tickMainThread.
  glm::vec2 _cameraBase{0.0f};
  float _cameraZoom = 1.0f;
  glm::vec2 _pointer{0.0f};  // framebuffer px, top-left origin
  bool _pointerSeen = false;
  int _pendingRelease = -1;  // the driver's click: its button goes up next frame
  bool _debugPhysics = false;  // JM_DEBUG_PHYSICS, or the driver's `debug physics on`
  std::vector<std::future<bool>> _writes;  // writeImageLater's, until done
  float _shakeAmplitude = 0.0f, _shakeDuration = 0.0f, _shakeRemaining = 0.0f;
  std::mt19937 _shakeRng;  // seeded from the run's seeds

  uint64_t _frame = 0;
  std::optional<EditorView> _editorView;

  void registerAssetTypes(Engine& app);
  void captureIfRequested(const Engine& app);
  // The driver's move, click, mousedown, mouseup and wheel commands.
  nlohmann::json pointerCommand(Engine& app, std::string_view verb, std::string_view args);
  // The last frame as an image (.png, or .jpg), at most maxWidth wide (0: as drawn).
  bool writeImage(const std::filesystem::path& path, int maxWidth = 0);
  // The same, written on a worker thread (the engine's capture requests).
  void writeImageLater(const std::filesystem::path& path, int maxWidth);
  void writeCaptures(Engine& app);  // the engine's capture requests so far
  bool writeFrame(const std::string& path);  // the last frame as a PNG
  // A script's sprite.setTexture: the sprite shows `reference` (and stops animating).
  void setSpriteTexture(World& world, EntityId entity, const std::string& reference);
  // Points the sprite at an image reference; false (logged) if it doesn't resolve.
  bool setSpriteImage(SpriteComponent& sprite, const std::string& reference);
  // A loaded .frag by path or short name ("crt").
  ShaderHandle shaderFor(std::string_view nameOrPath) const;
};
