#pragma once

#include <array>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
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
    _screenShader = createShader(screen_vertex_shader, screen_fragment_shader);

    uint8_t image[16] = {255, 255, 255, 255};
    _defaultTexture = createTexture(1, 1, 4, image);
    _batches.try_emplace(_defaultTexture);
    SpriteBatch& newBatch = _batches.at(_defaultTexture);
    auto& texture = _textures.at(_defaultTexture);
    newBatch.initialize();
    newBatch.setTexture(&texture);

    return true;
  }

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

    // @TODO: loop through enabled effects, swapping the _swapIndex back and forth

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