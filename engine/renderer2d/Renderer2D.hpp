#pragma once

#include <optional>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "Camera2D.hpp"
#include "Lights.hpp"
#include "GpuResources.hpp"
#include "Shadows.hpp"
#include "SpriteBatch.hpp"
#include "SpriteInstance.hpp"
#include "gl/FrameBuffer.hpp"
#include "gl/VertexArray.hpp"
#include "posteffects/PostEffectChain.hpp"

// The game sees a fixed logical resolution (world units = logical pixels at zoom
// 1), scaled to the framebuffer and letterboxed.
struct RenderSettings {
  int logicalWidth = 0;  // 0 = framebuffer size
  int logicalHeight = 0;
  glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f};
  glm::vec4 letterboxColor{0.0f, 0.0f, 0.0f, 1.0f};
};

// Frame pipeline: endFrame draws z-sorted sprites, then UI quads, then post-effects
// and any transition. draw* only queue (any thread); the rest is main thread only.
class Renderer2D {
 public:
  // gpu = false: no OpenGL at all (a run with no window, JM_RENDERER=none).
  // Frames are gathered and sorted as usual and kept as data (drawn*), but
  // nothing is drawn, and there are no pixels to capture.
  bool initialize(int framebufferWidth, int framebufferHeight, const RenderSettings& settings, bool gpu = true);
  bool gpu() const { return _gpu; }
  void shutdown();

  GpuResources& resources() { return _resources; }
  PostEffectChain& chain() { return _chain; }
  Camera2D& camera() { return _camera; }

  void resize(int framebufferWidth, int framebufferHeight);
  // Replaces the configured logical size (an editor's free view); nullopt restores it.
  void setLogicalSizeOverride(std::optional<glm::ivec2> size);
  // Off = frames stay offscreen for a host to draw (frameTexture), e.g. an editor.
  void setPresentsToScreen(bool on) { _presentsToScreen = on; }
  // The last finished frame (letterboxed, effects applied); GL texture id, rows bottom first.
  unsigned frameTexture() const { return _gpu ? _swap[_current].color().id() : 0; }
  glm::ivec2 frameSize() const { return {_width, _height}; }
  // The letterboxed game area inside the frame, framebuffer px (x, y from bottom-left, w, h).
  glm::vec4 gameViewport() const { return _viewport; }
  void setClearColor(const glm::vec4& color) { _settings.clearColor = color; }
  // Shaders' u_time: the game's unscaled seconds, not the wall clock, so a
  // replayed run draws the same frames.
  void setTime(float seconds) { _time = seconds; }
  glm::ivec2 logicalSize() const { return {_logicalW, _logicalH}; }
  float pixelScale() const { return _viewport.z / static_cast<float>(_logicalW); }  // framebuffer px per logical px
  TextureHandle whiteTexture() const { return _white; }

  // Lit::No keeps it as drawn whatever the lighting (debug overlays). A
  // texture with a normal map (setNormalMap) is lit by it.
  enum class Lit { Yes, No };
  void drawSprite(const glm::mat4& transform, const glm::vec4& color, const glm::vec4& texRect, TextureHandle texture,
                  float z, Lit lit = Lit::Yes);
  // A line from a to b, `width` world units wide, at z: a thin solid quad.
  void drawLine(glm::vec2 a, glm::vec2 b, const glm::vec4& color, float width, float z, Lit lit = Lit::Yes);
  // What this frame's world sprites are lit by (the UI never is).
  void setLighting(Lighting lighting) { _lighting = std::move(lighting); }
  // Lights `texture`, wherever it's drawn (any of its regions), by `normal`: tangent-space, y up.
  void setNormalMap(TextureHandle texture, TextureHandle normal) { _normals[texture] = normal; }
  TextureHandle normalMap(TextureHandle texture) const;
  const Lighting& lighting() const { return _lighting; }
  // rect = (x, y, w, h) in logical pixels.
  void drawScreenQuad(const glm::vec4& rect, const glm::vec4& color, const glm::vec4& texRect, TextureHandle texture);

  // Maps later screen quads: rect * scale + offset (an editor drawing the
  // game's UI into the game frame on its canvas). Identity by default.
  struct ScreenTransform {
    glm::vec2 offset{0.0f};
    float scale = 1.0f;
  };
  void setScreenTransform(ScreenTransform transform) { _screenTransform = transform; }
  // Framebuffer pixels per screen-quad unit, transform included (for crisp text).
  float screenPixelScale() const { return pixelScale() * _screenTransform.scale; }

  // Transitions: `shader` invalid = crossfade. progress runs 0 (old) → 1 (new).
  void beginTransition(ShaderHandle shader);
  void setTransitionProgress(float progress);
  void endTransition();

  // Off: frames are collected (the draw list) but not drawn (a session
  // replay fast-forwarding to where the player takes over).
  void setDrawing(bool on) { _drawing = on; }
  void endFrame();

  // Last presented frame as RGBA rows, top first (slow: for captures/tests).
  // Empty without a GPU.
  std::vector<uint8_t> readFinalFrame(int& width, int& height);

  // What the last finished frame drew: world sprites back to front, then
  // screen quads (UI, logical px), as the GPU got them.
  struct DrawItem {
    SpriteInstance instance;
    TextureHandle texture;
    float z;
    Lit lit = Lit::Yes;
    TextureHandle normal;  // invalid: none
    // Drawn in one instanced call with `other` when they're next to each other.
    bool batchesWith(const DrawItem& other) const {
      return texture == other.texture && normal == other.normal && lit == other.lit;
    }
  };
  const std::vector<DrawItem>& drawnWorld() const { return _drawnWorld; }
  const std::vector<DrawItem>& drawnScreen() const { return _drawnScreen; }

 private:
  struct Transition {
    TextureHandle oldFrame;
    ShaderHandle shader;
    float progress = 0.0f;
  };

  GpuResources _resources;
  RenderSettings _settings;
  Camera2D _camera;
  Lighting _lighting;
  std::unordered_map<TextureHandle, TextureHandle> _normals;  // texture -> its normal map
  ShadowMap _shadows;
  std::vector<const Lighting::Light*> shownLights() const;
  // Sets the sprite shader's lighting; draws the shadow map first if a shown light casts shadows.
  void applyLighting(gl::Shader& sprite);
  gl::Shader* _litShader = nullptr;  // while drawing world items with lighting on: drawItems switches u_lit
  SpriteBatch _batch;
  std::vector<SpriteInstance> _instances;  // a pass's, gathered for one upload
  std::vector<DrawItem> _worldItems;
  std::vector<DrawItem> _screenItems;
  std::vector<DrawItem> _drawnWorld, _drawnScreen;  // the last frame's (swapped in, not copied)
  bool _gpu = true;

  int _width = 0, _height = 0;  // framebuffer
  std::optional<glm::ivec2> _logicalOverride;
  ScreenTransform _screenTransform;
  bool _drawing = true;
  bool _presentsToScreen = true;
  int _logicalW = 1, _logicalH = 1;
  glm::vec4 _viewport{0.0f};  // letterboxed game area, framebuffer px

  ShaderHandle _spriteShader;
  ShaderHandle _crossfade;
  TextureHandle _white;

  // The scene renders into _swap[0]; each fullscreen pass draws into the other.
  gl::FrameBuffer _swap[2];
  int _current = 0;  // which one holds the latest image

  PostEffectChain _chain;
  std::optional<Transition> _transition;
  gl::VertexArray _quad;
  float _time = 0.0f;

  TextureHandle copyFinalFrame();
  void drawFrame();  // GPU: scene, effects, transition, present
  void renderScene();
  void drawItems(const std::vector<DrawItem>& items);
  void fullscreenPass(gl::Shader& shader, TextureHandle aux, const PostEffect* effect, float progress);
  void present();
};
