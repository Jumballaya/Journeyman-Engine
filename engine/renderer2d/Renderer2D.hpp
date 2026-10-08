#pragma once

#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "Camera2D.hpp"
#include "GpuResources.hpp"
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
  bool initialize(int framebufferWidth, int framebufferHeight, const RenderSettings& settings);
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
  unsigned frameTexture() const { return _swap[_current].color().id(); }
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

  void drawSprite(const glm::mat4& transform, const glm::vec4& color, const glm::vec4& texRect, TextureHandle texture,
                  float z);
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

  void endFrame();

  // Last presented frame as RGBA rows, top first (slow: for captures/tests).
  std::vector<uint8_t> readFinalFrame(int& width, int& height);

 private:
  struct DrawItem {
    SpriteInstance instance;
    TextureHandle texture;
    float z;
  };
  struct Transition {
    TextureHandle oldFrame;
    ShaderHandle shader;
    float progress = 0.0f;
  };

  GpuResources _resources;
  RenderSettings _settings;
  Camera2D _camera;
  SpriteBatch _batch;
  std::vector<SpriteInstance> _instances;  // a pass's, gathered for one upload
  std::vector<DrawItem> _worldItems;
  std::vector<DrawItem> _screenItems;

  int _width = 0, _height = 0;  // framebuffer
  std::optional<glm::ivec2> _logicalOverride;
  ScreenTransform _screenTransform;
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
  void renderScene();
  void drawItems(const std::vector<DrawItem>& items);
  void fullscreenPass(gl::Shader& shader, TextureHandle aux, const PostEffect* effect, float progress);
  void present();
};
