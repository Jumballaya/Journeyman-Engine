#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "../core/logger/logging.hpp"
#include "Camera2D.hpp"
#include "ShaderHandle.hpp"
#include "SpriteBatch.hpp"
#include "SpriteInstance.hpp"
#include "Surface.hpp"
#include "TextureHandle.hpp"
#include "common.hpp"
#include "gl/GLBuffer.hpp"
#include "gl/Shader.hpp"
#include "gl/Texture2D.hpp"
#include "gl/VertexArray.hpp"
#include "posteffects/PostEffect.hpp"
#include "posteffects/PostEffectChain.hpp"
#include "shaders.hpp"

// How the game maps onto the window. The game always sees a fixed logical
// resolution (world units == logical pixels at zoom 1); the renderer scales
// it to the framebuffer and letterboxes to preserve aspect, so the same game
// looks identical on a Retina laptop, a 1080p monitor, or fullscreen.
struct RenderSettings {
  int logicalWidth = 0;   // 0 = use the framebuffer size
  int logicalHeight = 0;
  glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f};
  glm::vec4 letterboxColor{0.0f, 0.0f, 0.0f, 1.0f};
};

// Sprite renderer + post-processing. Frame structure (endFrame):
//   1. world pass   sprites sorted by z (then texture, for batching)
//   2. screen pass  UI quads in submission order, top-left origin, y down
//   3. post chain   script/scene effects, ping-ponged
//   4. transition   old-frame snapshot composited over the new scene
//   5. present      blit to the window
// Passes 1–2 render into the scene surface, so transitions and post-effects
// also apply to UI. All methods are main-thread-only except drawSprite /
// drawScreenQuad, which only append to CPU-side lists.
class Renderer2D {
 public:
  bool initialize(int framebufferWidth, int framebufferHeight, const RenderSettings& settings) {
    if (!gladLoadGL(glfwGetProcAddress)) {
      JM_LOG_ERROR("[Renderer2D] OpenGL failed to load");
      return false;
    }
    _settings = settings;

    _spriteShader = createShader(sprite_vertex_shader, sprite_fragment_shader);
    _shaders.at(_spriteShader).bindUniformBlock("Camera", Camera2D::bindingPoint);
    _screenShader = createShader(screen_vertex_shader, screen_fragment_shader);

    uint8_t white[4] = {255, 255, 255, 255};
    _defaultTexture = createTexture(1, 1, 4, white);

    _batch.initialize();
    _screenUbo.initialize(gl::BufferType::Uniform, gl::BufferUsage::DynamicDraw);
    initializeFullscreenQuad();
    resizeTargets(framebufferWidth, framebufferHeight);
    _startTime = std::chrono::steady_clock::now();
    return true;
  }

  PostEffectChain& chain() { return _chain; }
  const PostEffectChain& chain() const { return _chain; }
  Camera2D& camera() { return _camera; }

  void setClearColor(const glm::vec4& color) { _settings.clearColor = color; }

  glm::ivec2 logicalSize() const { return {_logicalW, _logicalH}; }
  // Framebuffer pixels per logical pixel (e.g. 2.5 on a Retina window).
  float pixelScale() const { return _viewport.z / static_cast<float>(_logicalW); }

  // World sprite. `z` orders drawing (higher on top); equal z keeps
  // submission order stable within a texture.
  void drawSprite(const glm::mat4& transform, const glm::vec4& color, const glm::vec4& texRect,
                  float layer, TextureHandle tex, float z = 0.0f) {
    _worldItems.push_back({SpriteInstance{transform, color, texRect, layer}, resolve(tex), z});
  }

  // Screen-space quad in logical pixels: rect = (x, y, w, h), origin top-left.
  void drawScreenQuad(const glm::vec4& rect, const glm::vec4& color, const glm::vec4& texRect,
                      TextureHandle tex) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(rect.x + rect.z * 0.5f, rect.y + rect.w * 0.5f, 0.0f));
    // Negative y scale: the projection is y-down but textures are authored
    // for the y-up world pass.
    m = glm::scale(m, glm::vec3(rect.z * 0.5f, -rect.w * 0.5f, 1.0f));
    _screenItems.push_back({SpriteInstance{m, color, texRect, 0.0f}, resolve(tex), 0.0f});
  }

  // Scene transition: composite `from` (old frame) with the live frame using
  // `shader`; progress 0 → 1. Pass an invalid shader for the crossfade.
  void setTransition(TextureHandle from, ShaderHandle shader, float progress) {
    _transition = Transition{from, shader.isValid() ? shader : _crossfadeShader, progress};
  }
  void clearTransition() { _transition.reset(); }

  void endFrame() {
    renderScene();
    _swapIndex = 0;
    blit(_sceneSurface, _swapchain[_swapIndex]);
    applyEffectChain();
    applyTransition();
    present(_swapchain[_swapIndex]);
    _worldItems.clear();
    _screenItems.clear();
  }

  void resizeTargets(int w, int h) {
    if (w <= 0 || h <= 0) return;  // minimized
    _width = w;
    _height = h;
    _logicalW = _settings.logicalWidth > 0 ? _settings.logicalWidth : w;
    _logicalH = _settings.logicalHeight > 0 ? _settings.logicalHeight : h;

    const float scale = std::min(static_cast<float>(w) / _logicalW, static_cast<float>(h) / _logicalH);
    const float vw = std::floor(_logicalW * scale);
    const float vh = std::floor(_logicalH * scale);
    _viewport = glm::vec4(std::floor((w - vw) * 0.5f), std::floor((h - vh) * 0.5f), vw, vh);

    if (_sceneSurface.width() == 0) {
      _sceneSurface.initialize(w, h);
      _swapchain[0].initialize(w, h);
      _swapchain[1].initialize(w, h);
      _camera.initialize(_logicalW, _logicalH);
    } else {
      _sceneSurface.resize(w, h);
      _swapchain[0].resize(w, h);
      _swapchain[1].resize(w, h);
    }
    _camera.setViewport(_logicalW, _logicalH);
  }

  //
  //  GL ASSET MANAGEMENT
  //
  TextureHandle createTexture(int width, int height, int channels, void* data, bool linear = false) {
    GLenum internalFormat = GL_RGBA8;
    GLenum format = GL_RGBA;
    switch (channels) {
      case 1: internalFormat = GL_R8; format = GL_RED; break;
      case 3: internalFormat = GL_RGB8; format = GL_RGB; break;
      case 4: break;
      default:
        JM_LOG_ERROR("[Texture] Unsupported channel count {}", channels);
        return {};
    }
    gl::Texture2D tex;
    tex.initialize(width, height, internalFormat, format, GL_UNSIGNED_BYTE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    tex.setData(data);
    if (linear) tex.setFilter(GL_LINEAR);
    return storeTexture(std::move(tex));
  }

  // Empty RGBA texture for incremental uploads (dynamic atlases, glyphs).
  TextureHandle createEmptyTexture(int width, int height, std::string_view filter) {
    gl::Texture2D tex;
    tex.initialize(width, height, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
    std::vector<uint8_t> zeros(static_cast<size_t>(width) * height * 4, 0);
    tex.setData(zeros.data());
    if (filter == "linear") tex.setFilter(GL_LINEAR);
    return storeTexture(std::move(tex));
  }

  bool subUploadTexture(TextureHandle handle, int x, int y, int w, int h, const void* pixels) {
    auto it = _textures.find(handle);
    if (it == _textures.end()) {
      JM_LOG_ERROR("[Renderer2D] subUploadTexture: unknown TextureHandle");
      return false;
    }
    it->second.subUpload(x, y, w, h, pixels);
    return true;
  }

  ShaderHandle createShader(const std::string& vertex, const std::string& fragment) {
    gl::Shader shader;
    shader.initialize();
    shader.loadShader(vertex, fragment);
    ShaderHandle handle;
    handle.id = _nextShaderId++;
    _shaders.emplace(handle, std::move(shader));
    return handle;
  }

  // Compiles a post-effect/transition fragment shader. Sources without a
  // #version get post_effect_prelude prepended. Returns an invalid handle
  // (and logs the compiler output) on failure.
  ShaderHandle createPostShader(std::string_view fragment, std::string_view debugName) {
    std::string source = fragment.find("#version") == std::string_view::npos
                             ? std::string(post_effect_prelude) + std::string(fragment)
                             : std::string(fragment);
    try {
      return createShader(screen_vertex_shader, source);
    } catch (const std::exception& e) {
      JM_LOG_ERROR("[Renderer2D] shader '{}' failed to compile:\n{}", debugName, e.what());
      return {};
    }
  }

  TextureHandle getDefaultTexture() const { return _defaultTexture; }

  glm::vec2 textureSize(TextureHandle handle) const {
    auto it = _textures.find(handle);
    if (it == _textures.end()) return glm::vec2(0.0f);
    return glm::vec2(static_cast<float>(it->second.width()), static_cast<float>(it->second.height()));
  }

  // Copy of the last presented frame (post-effects included). Used as the
  // outgoing scene in transitions. Release with releaseTexture.
  TextureHandle captureFinalFrame() {
    const Surface& src = _swapchain[_swapIndex];
    gl::Texture2D tex;
    tex.initialize(src.width(), src.height());
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex.id(), 0);
    src.bindRead();
    glBlitFramebuffer(0, 0, src.width(), src.height(), 0, 0, src.width(), src.height(), GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    return storeTexture(std::move(tex));
  }

  void releaseTexture(TextureHandle handle) { _textures.erase(handle); }

  // Reads back the last presented frame as tightly packed RGBA rows, top row
  // first. Main thread; slow (stalls the GPU) — for captures/tests only.
  std::vector<uint8_t> readFinalFrame(int& width, int& height) {
    const Surface& src = _swapchain[_swapIndex];
    width = src.width();
    height = src.height();
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    src.bindRead();
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    const size_t row = static_cast<size_t>(width) * 4;
    for (int y = 0; y < height / 2; ++y) {
      std::swap_ranges(pixels.begin() + y * row, pixels.begin() + (y + 1) * row,
                       pixels.begin() + (height - 1 - y) * row);
    }
    return pixels;
  }

  void setCrossfadeShader(ShaderHandle shader) { _crossfadeShader = shader; }

  void shutdown() {
    _textures.clear();
    _shaders.clear();
    _batch.destroy();
    _screenUbo.destroy();
    _camera.destroy();
    _sceneSurface.destroy();
    _swapchain[0].destroy();
    _swapchain[1].destroy();
    _quadVAO.destroy();
  }

 private:
  struct DrawItem {
    SpriteInstance instance;
    TextureHandle texture;
    float z;
  };
  struct Transition {
    TextureHandle from;
    ShaderHandle shader;
    float progress;
  };

  RenderSettings _settings;
  std::unordered_map<TextureHandle, gl::Texture2D> _textures;
  std::unordered_map<ShaderHandle, gl::Shader> _shaders;
  SpriteBatch _batch;
  std::vector<DrawItem> _worldItems;
  std::vector<DrawItem> _screenItems;

  Camera2D _camera;
  gl::GLBuffer _screenUbo;

  uint32_t _nextTextureId = 1;
  uint32_t _nextShaderId = 1;

  int _width = 0, _height = 0;          // framebuffer
  int _logicalW = 1, _logicalH = 1;
  glm::vec4 _viewport{0.0f};            // letterboxed game area, framebuffer px

  ShaderHandle _spriteShader;
  ShaderHandle _screenShader;
  ShaderHandle _crossfadeShader;
  TextureHandle _defaultTexture;

  Surface _sceneSurface;
  Surface _swapchain[2];
  int _swapIndex = 0;

  PostEffectChain _chain;
  std::optional<Transition> _transition;
  gl::VertexArray _quadVAO;
  std::chrono::steady_clock::time_point _startTime;

  TextureHandle storeTexture(gl::Texture2D&& tex) {
    TextureHandle handle;
    handle.id = _nextTextureId++;
    handle.type = TextureHandle::Type::_2D;
    _textures.emplace(handle, std::move(tex));
    return handle;
  }

  TextureHandle resolve(TextureHandle tex) const {
    return tex.isValid() ? tex : _defaultTexture;
  }

  float seconds() const {
    return std::chrono::duration<float>(std::chrono::steady_clock::now() - _startTime).count();
  }

  void initializeFullscreenQuad() {
    static constexpr std::array<float, 20> quadVerts = {
        -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
        1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 0.0f, 1.0f, 1.0f};
    static constexpr std::array<gl::VertexLayout, 2> layout = {
        gl::VertexLayout{3, GL_FLOAT, false, 0},
        gl::VertexLayout{2, GL_FLOAT, false, 3 * sizeof(float)}};
    _quadVAO.initialize();
    _quadVAO.setVertexData(quadVerts, 5 * sizeof(float), layout);
  }

  // Draws consecutive same-texture items as one instanced call.
  void drawItems(const std::vector<DrawItem>& items) {
    size_t i = 0;
    while (i < items.size()) {
      const TextureHandle tex = items[i].texture;
      _batch.flush();
      while (i < items.size() && items[i].texture == tex) {
        _batch.submit(items[i].instance);
        ++i;
      }
      auto it = _textures.find(tex);
      if (it == _textures.end()) it = _textures.find(_defaultTexture);
      it->second.bindToSlot(0);
      _batch.draw();
    }
    _batch.flush();
  }

  void renderScene() {
    _sceneSurface.bind();
    glViewport(0, 0, _sceneSurface.width(), _sceneSurface.height());
    const auto& lb = _settings.letterboxColor;
    _sceneSurface.clear(lb.r, lb.g, lb.b, lb.a, true);

    glEnable(GL_SCISSOR_TEST);
    glScissor(static_cast<GLint>(_viewport.x), static_cast<GLint>(_viewport.y),
              static_cast<GLsizei>(_viewport.z), static_cast<GLsizei>(_viewport.w));
    const auto& cc = _settings.clearColor;
    glClearColor(cc.r, cc.g, cc.b, cc.a);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(static_cast<GLint>(_viewport.x), static_cast<GLint>(_viewport.y),
               static_cast<GLsizei>(_viewport.z), static_cast<GLsizei>(_viewport.w));

    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    auto& sprite = _shaders.at(_spriteShader);
    sprite.bind();
    sprite.uniform("u_texture", 0);

    // World pass.
    _camera.upload();
    std::stable_sort(_worldItems.begin(), _worldItems.end(), [](const DrawItem& a, const DrawItem& b) {
      if (a.z != b.z) return a.z < b.z;
      return a.texture.id < b.texture.id;
    });
    drawItems(_worldItems);

    // Screen pass: same shader, y-down logical-pixel projection.
    Camera2D::CameraBlock block;
    block.proj = glm::ortho(0.0f, static_cast<float>(_logicalW), static_cast<float>(_logicalH), 0.0f, -1.0f, 1.0f);
    block.view = glm::mat4(1.0f);
    block.projView = block.proj;
    block.viewport = glm::vec4(static_cast<float>(_logicalW), static_cast<float>(_logicalH), 0.0f, 0.0f);
    _screenUbo.bind();
    _screenUbo.setData(&block, sizeof(block));
    glBindBufferBase(GL_UNIFORM_BUFFER, Camera2D::bindingPoint, _screenUbo.id());
    drawItems(_screenItems);

    sprite.unbind();
    glDisable(GL_SCISSOR_TEST);
  }

  // Runs one fullscreen pass of `shader` from the current swap surface into
  // the other, then swaps. `aux` is bound to u_aux when valid.
  void fullscreenPass(gl::Shader& shader, TextureHandle aux,
                      const std::unordered_map<std::string, UniformValue>* uniforms, float progress) {
    Surface& src = _swapchain[_swapIndex];
    Surface& dst = _swapchain[_swapIndex ^ 1];
    dst.bind();
    glViewport(0, 0, dst.width(), dst.height());
    dst.clear(0.0f, 0.0f, 0.0f, 1.0f, true);

    shader.bind();
    src.color().bindToSlot(0);
    shader.uniform("u_primary", 0);
    if (aux.isValid()) {
      if (auto it = _textures.find(aux); it != _textures.end()) {
        it->second.bindToSlot(1);
        shader.uniform("u_aux", 1);
      }
    }
    shader.uniform("u_resolution", glm::vec2(static_cast<float>(dst.width()), static_cast<float>(dst.height())));
    shader.uniform("u_viewport", _viewport);
    shader.uniform("u_logical", glm::vec2(static_cast<float>(_logicalW), static_cast<float>(_logicalH)));
    shader.uniform("u_time", seconds());
    shader.uniform("u_progress", progress);
    if (uniforms) {
      for (const auto& [name, value] : *uniforms) {
        std::visit([&](const auto& v) { shader.uniform(name, v); }, value);
      }
    }
    _quadVAO.bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    shader.unbind();
    _swapIndex ^= 1;
  }

  void applyEffectChain() {
    glDisable(GL_BLEND);
    for (const PostEffect* effect : _chain.enabledEffects()) {
      auto it = _shaders.find(effect->shader);
      if (it == _shaders.end()) continue;
      fullscreenPass(it->second, effect->auxTexture.value_or(TextureHandle{}), &effect->uniforms, 0.0f);
    }
  }

  void applyTransition() {
    if (!_transition) return;
    auto it = _shaders.find(_transition->shader);
    if (it == _shaders.end()) return;
    glDisable(GL_BLEND);
    fullscreenPass(it->second, _transition->from, nullptr, _transition->progress);
  }

  void blit(const Surface& src, Surface& dst) {
    src.bindRead();
    dst.bindDraw();
    glBlitFramebuffer(0, 0, src.width(), src.height(), 0, 0, dst.width(), dst.height(), GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  }

  void present(const Surface& src) {
    src.bindRead();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, src.width(), src.height(), 0, 0, _width, _height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  }
};
