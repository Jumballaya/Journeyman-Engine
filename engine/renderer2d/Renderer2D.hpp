#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "Camera2D.hpp"
#include "GpuResources.hpp"
#include "SpriteBatch.hpp"
#include "SpriteInstance.hpp"
#include "Surface.hpp"
#include "gl/GLBuffer.hpp"
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
  void setClearColor(const glm::vec4& color) { _settings.clearColor = color; }
  glm::ivec2 logicalSize() const { return {_logicalW, _logicalH}; }
  float pixelScale() const { return _viewport.z / static_cast<float>(_logicalW); }  // framebuffer px per logical px
  TextureHandle whiteTexture() const { return _white; }

  void drawSprite(const glm::mat4& transform, const glm::vec4& color, const glm::vec4& texRect, TextureHandle texture,
                  float z);
  // rect = (x, y, w, h) in logical pixels.
  void drawScreenQuad(const glm::vec4& rect, const glm::vec4& color, const glm::vec4& texRect, TextureHandle texture);

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
  gl::GLBuffer _screenUbo;
  SpriteBatch _batch;
  std::mutex _worldMutex;  // render systems draw in parallel
  std::vector<DrawItem> _worldItems;
  std::vector<DrawItem> _screenItems;

  int _width = 0, _height = 0;  // framebuffer
  int _logicalW = 1, _logicalH = 1;
  glm::vec4 _viewport{0.0f};    // letterboxed game area, framebuffer px

  ShaderHandle _spriteShader;
  ShaderHandle _crossfade;
  TextureHandle _white;

  Surface _scene;
  Surface _swap[2];
  int _current = 0;  // which swap surface holds the latest image

  PostEffectChain _chain;
  std::optional<Transition> _transition;
  gl::VertexArray _quad;
  std::chrono::steady_clock::time_point _start;

  TextureHandle copyFinalFrame();
  void renderScene();
  void drawItems(const std::vector<DrawItem>& items);
  void fullscreenPass(gl::Shader& shader, TextureHandle aux, const PostEffect* effect, float progress);
  void blit(const Surface& from, Surface& to);
  void present();
};
