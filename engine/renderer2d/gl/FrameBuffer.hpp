#pragma once

#include <glm/glm.hpp>

#include "../../core/logger/logging.hpp"
#include "../common.hpp"
#include "Texture2D.hpp"

namespace gl {

// A framebuffer that renders into one RGBA8 color texture.
struct FrameBuffer {
  FrameBuffer() = default;
  ~FrameBuffer() { destroy(); }
  FrameBuffer(const FrameBuffer&) = delete;
  FrameBuffer& operator=(const FrameBuffer&) = delete;

  // Creates the framebuffer on first use; a new size reallocates the texture.
  void resize(int width, int height) {
    if (_fbo && width == _color.width() && height == _color.height()) return;
    if (!_fbo) glGenFramebuffers(1, &_fbo);
    _color.initialize(width, height);
    bind();
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _color.id(), 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      JM_LOG_ERROR("[Renderer2D] framebuffer {}x{} is incomplete", width, height);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void bind(GLenum target = GL_FRAMEBUFFER) const { glBindFramebuffer(target, _fbo); }

  // Binds the framebuffer and fills it with `color`.
  void clear(const glm::vec4& color) const {
    bind();
    glClearColor(color.r, color.g, color.b, color.a);
    glClear(GL_COLOR_BUFFER_BIT);
  }

  Texture2D& color() { return _color; }
  const Texture2D& color() const { return _color; }
  int width() const { return _color.width(); }
  int height() const { return _color.height(); }

  void destroy() {
    if (_fbo) glDeleteFramebuffers(1, &_fbo);
    _fbo = 0;
    _color.destroy();
  }

 private:
  GLuint _fbo = 0;
  Texture2D _color;
};

}  // namespace gl
