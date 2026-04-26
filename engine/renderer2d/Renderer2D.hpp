#pragma once

#include <array>
#include <chrono>
#include <stdexcept>
#include <string>
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
#include "gl/FrameBuffer.hpp"
#include "gl/GLBuffer.hpp"
#include "gl/Shader.hpp"
#include "gl/Texture2D.hpp"
#include "gl/VertexArray.hpp"
#include "posteffects/PostEffect.hpp"
#include "posteffects/PostEffectChain.hpp"
#include "shaders.hpp"

//
//  @TODO: Eventually tear out the RendererCommandQueue or whatever it will be called
//         as well as the textures, shaders, spritebatch, etc.
//         Maybe a RenderResources class ? or just part of the eventual Renderer2DModule
//         Then, turn this into a sprite renderer static class
//
//                Renderer: Base clase, abstracts away glXXX calls
//                SpriteRenderer: Inherits from Renderer and renders a sprite batch
//                LineRenderer: Inherits from Renderer and renders a line batch
//                ShapeRenderer: Inherits from Renderer and renders shape batches
//
//
//          This way we can submit commands, and use the appropriate renderer during the
//          main thread draw calls
//
class Renderer2D {
 public:
  Renderer2D() = default;
  ~Renderer2D() = default;

  bool initialize(int width, int height) {
    if (!gladLoadGL(glfwGetProcAddress)) {
      JM_LOG_ERROR("[Renderer2D] OpenGL failed to load");
      return false;
    }

    resizeTargets(width, height);
    _spriteShader = createShader(sprite_vertex_shader, sprite_fragment_shader);
#ifdef __APPLE__
    _shaders.at(_spriteShader).bindUniformBlock("Camera", Camera2D::bindingPoint);
#endif
    _screenShader = createShader(screen_vertex_shader, screen_fragment_shader);

    uint8_t image[16] = {255, 255, 255, 255};
    _defaultTexture = createTexture(1, 1, 4, image);
    _batches.try_emplace(_defaultTexture);
    SpriteBatch& newBatch = _batches.at(_defaultTexture);
    auto& texture = _textures.at(_defaultTexture);
    newBatch.initialize();
    newBatch.setTexture(&texture);

    initializeFullscreenQuad();
    _startTime = std::chrono::steady_clock::now();

    return true;
  }

  PostEffectChain& chain() { return _chain; }
  const PostEffectChain& chain() const { return _chain; }

  Camera2D& camera() {
    return _camera;
  }

  void endFrame() {
    _camera.upload();

    auto spriteShader = _shaders.find(_spriteShader);
    if (spriteShader == _shaders.end()) {
      return;
    }
    auto screenShader = _shaders.find(_screenShader);
    if (screenShader == _shaders.end()) {
      return;
    }

    _sceneSurface.bind();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, _sceneSurface.width(), _sceneSurface.height());
    _sceneSurface.clear(0.0f, 0.0f, 0.0f, 0.0f, true);

    spriteShader->second.bind();
    spriteShader->second.uniform("u_texture", 0);
    for (auto& [texHandle, batch] : _batches) {
      auto itTex = _textures.find(texHandle);
      if (itTex == _textures.end()) {
        JM_LOG_ERROR("[Renderer2D] Texture not found for batch");
        auto itDefTex = _textures.find(_defaultTexture);
        if (itDefTex == _textures.end()) {
          JM_LOG_ERROR("[Renderer2D] Default texture not found");
          continue;
        }
        itDefTex->second.bindToSlot(0);
        batch.setTexture(&(itDefTex->second));
      } else {
        itTex->second.bindToSlot(0);
        batch.setTexture(&(itTex->second));
      }
      batch.draw();
    }

    spriteShader->second.unbind();

    screenShader->second.bind();
    blitSurface(_sceneSurface, _swapchain[_swapIndex]);
    screenShader->second.unbind();

    applyEffectChain();

    screenShader->second.bind();
    presentToDefault(_swapchain[_swapIndex]);
    screenShader->second.unbind();

    for (auto& batch : _batches) {
      batch.second.flush();
    }
  }

  void drawSprite(
      const glm::mat4& transform,
      const glm::vec4& color,
      const glm::vec4& texRect,
      float layer,
      TextureHandle tex) {
    auto batch = _batches.find(tex);
    if (batch == _batches.end()) {
      auto texture = _textures.find(tex);
      if (texture != _textures.end()) {
        _batches.try_emplace(tex);
        SpriteBatch& newBatch = _batches.at(tex);
        newBatch.initialize();
        newBatch.setTexture(&texture->second);
        newBatch.submit({transform, color, texRect, layer});
        return;
      } else {
        JM_LOG_DEBUG("[Renderer2D] Use of untracked TextureHandle, falling back to default texture");
        auto batch = _batches.find(_defaultTexture);
        batch->second.submit({transform, color, texRect, layer});
        return;
      }
    }
    batch->second.submit({transform, color, texRect, layer});
  }

  // Render Swap Chain

  void resizeTargets(int w, int h) {
    _width = w;
    _height = h;

    if (_sceneSurface.width() == 0) {
      _sceneSurface.initialize(w, h);
      _swapchain[0].initialize(w, h);
      _swapchain[1].initialize(w, h);
      _camera.initialize(w, h);
    } else {
      _sceneSurface.resize(w, h);
      _swapchain[0].resize(w, h);
      _swapchain[1].resize(w, h);
    }

    _camera.setViewport(w, h);
  }

  //
  //
  //  GL ASSET MANAGEMENT
  //
  //
  TextureHandle createTexture(int width, int height, int channels, void* data) {
    TextureHandle handle;
    handle.id = _nextTextureId++;
    handle.type = TextureHandle::Type::_2D;

    GLenum internalFormat = GL_RGBA8;
    GLenum format = GL_RGBA;
    GLenum type = GL_UNSIGNED_BYTE;

    switch (channels) {
      case 1:
        internalFormat = GL_R8;
        format = GL_RED;
        break;
      case 3:
        internalFormat = GL_RGB8;
        format = GL_RGB;
        break;
      case 4:
        internalFormat = GL_RGBA8;
        format = GL_RGBA;
        break;
      default:
        JM_LOG_ERROR("[Texture] Unsupported channel count {}", channels);
        return {};
    }

    gl::Texture2D tex;
    tex.initialize(width, height, internalFormat, format, type);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    tex.setData(data);

    _textures.emplace(handle, std::move(tex));
    _batches.try_emplace(handle);
    SpriteBatch& newBatch = _batches.at(handle);
    newBatch.initialize();
    newBatch.setTexture(&tex);
    return handle;
  }

  ShaderHandle createShader(const std::string& vertex, const std::string& fragment) {
    ShaderHandle handle;
    handle.id = _nextShaderId++;

    gl::Shader shader;
    shader.initialize();
    shader.loadShader(vertex, fragment);

    _shaders.emplace(handle, std::move(shader));

    return handle;
  }

  ShaderHandle createShaderFromFiles(const std::filesystem::path& vertex, const std::filesystem::path& fragment) {
    ShaderHandle handle;
    handle.id = _nextShaderId++;

    gl::Shader shader;
    shader.initialize();
    shader.bind();
    shader.loadShader(vertex, fragment);
    shader.unbind();

    _shaders.emplace(handle, std::move(shader));

    return handle;
  }

  TextureHandle getDefaultTexture() const {
    return _defaultTexture;
  }

  // Copy the current scene surface into a freshly-allocated texture and
  // register it under a new TextureHandle. Intended for scene transitions —
  // the snapshot is bound as the aux texture of a Crossfade post-effect while
  // the new scene renders live. Caller must release via releaseCapturedTexture
  // when the transition ends.
  //
  // Main-thread-only: allocates a GL texture and a temporary FBO, then blits.
  // GL 4.1 on macOS has no glCopyImageSubData, so we go through a draw-FBO.
  TextureHandle captureSceneFrame() {
    TextureHandle handle;
    handle.id = _nextTextureId++;
    handle.type = TextureHandle::Type::_2D;

    const int w = _sceneSurface.width();
    const int h = _sceneSurface.height();

    gl::Texture2D tex;
    tex.initialize(w, h);

    GLuint tmpFbo = 0;
    glGenFramebuffers(1, &tmpFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, tmpFbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex.id(), 0);

    _sceneSurface.bindRead();
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &tmpFbo);

    _textures.emplace(handle, std::move(tex));
    return handle;
  }

  // Free a texture previously returned by captureSceneFrame. Erasing from
  // _textures runs gl::Texture2D's destructor, which calls glDeleteTextures.
  // Main-thread-only.
  void releaseCapturedTexture(TextureHandle handle) {
    _textures.erase(handle);
  }

  void shutdown() {
    for (auto& texPair : _textures) {
      texPair.second.destroy();
    }
    for (auto& shaderPair : _shaders) {
      shaderPair.second.destroy();
    }
    for (auto& batchPair : _batches) {
      batchPair.second.destroy();
    }
    _camera.destroy();
    _sceneSurface.destroy();
    _swapchain[0].destroy();
    _swapchain[1].destroy();
    _quadVAO.destroy();
  }

 private:
  std::unordered_map<TextureHandle, gl::Texture2D> _textures;
  std::unordered_map<ShaderHandle, gl::Shader> _shaders;
  std::unordered_map<TextureHandle, SpriteBatch> _batches;

  Camera2D _camera;

  uint32_t _nextTextureId = 1;
  uint32_t _nextShaderId = 1;

  int _width = 0;
  int _height = 0;

  // Misc 2D Renderer Resources
  ShaderHandle _spriteShader;
  ShaderHandle _screenShader;
  TextureHandle _defaultTexture;

  // Swapchain
  Surface _sceneSurface;  // main layer where all sprites render
  Surface _swapchain[2];  // ping-pong for post effects
  int _swapIndex = 0;

  // Post-effects
  PostEffectChain _chain;
  gl::VertexArray _quadVAO;
  std::chrono::steady_clock::time_point _startTime;

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

  void applyEffectChain() {
    const auto effects = _chain.enabledEffects();
    if (effects.empty()) {
      return;
    }

    glDisable(GL_BLEND);
    const float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - _startTime).count();

    for (const PostEffect* effect : effects) {
      auto shaderIt = _shaders.find(effect->shader);
      if (shaderIt == _shaders.end()) {
        JM_LOG_ERROR("[Renderer2D] PostEffect shader not found, skipping effect");
        continue;
      }

      Surface& src = _swapchain[_swapIndex];
      Surface& dst = _swapchain[_swapIndex ^ 1];

      dst.bind();
      glViewport(0, 0, dst.width(), dst.height());
      dst.clear(0.0f, 0.0f, 0.0f, 0.0f, true);

      shaderIt->second.bind();

      src.color().bindToSlot(0);
      shaderIt->second.uniform("u_primary", 0);

      if (effect->auxTexture.has_value()) {
        auto auxIt = _textures.find(*effect->auxTexture);
        if (auxIt == _textures.end()) {
          JM_LOG_WARN("[Renderer2D] PostEffect aux texture not found, skipping aux bind");
        } else {
          auxIt->second.bindToSlot(1);
          shaderIt->second.uniform("u_aux", 1);
        }
      }

      shaderIt->second.uniform("u_resolution", glm::vec2(static_cast<float>(dst.width()), static_cast<float>(dst.height())));
      shaderIt->second.uniform("u_time", seconds);

      for (const auto& [name, value] : effect->uniforms) {
        std::visit([&](const auto& v) { shaderIt->second.uniform(name, v); }, value);
      }

      _quadVAO.bind();
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

      shaderIt->second.unbind();

      _swapIndex ^= 1;
    }
  }

  void blitSurface(const Surface& src, Surface& dst) {
    src.bindRead();
    dst.bindDraw();
    glBlitFramebuffer(0, 0, src.width(), src.height(), 0, 0, dst.width(), dst.height(), GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  }

  void presentToDefault(const Surface& src) {
    src.bindRead();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, src.width(), src.height(), 0, 0, _width, _height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  }
};