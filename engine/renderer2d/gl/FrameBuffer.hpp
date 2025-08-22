#pragma once

#include <iostream>
#include <utility>

#include "../common.hpp"
#include "DepthBuffer.hpp"
#include "Texture2D.hpp"

namespace gl {

struct FrameBuffer {
 public:
  FrameBuffer() = default;

  ~FrameBuffer() {
    if (isValid()) {
      glDeleteFramebuffers(1, &_fbo);
    }
  }

  FrameBuffer(const FrameBuffer&) = delete;
  FrameBuffer& operator=(const FrameBuffer&) = delete;

  FrameBuffer(FrameBuffer&& other) noexcept
      : _fbo(other._fbo), _color(std::move(other._color)), _depth(std::move(other._depth)), _width(other._width), _height(other._height) {
    other._fbo = 0;
    other._width = 0;
    other._height = 0;
  }

  FrameBuffer& operator=(FrameBuffer&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    if (isValid()) {
      glDeleteFramebuffers(1, &_fbo);
    }

    _fbo = other._fbo;
    _color = std::move(other._color);
    _depth = std::move(other._depth);
    _width = other._width;
    _height = other._height;

    other._fbo = 0;
    other._width = 0;
    other._height = 0;

    return *this;
  }

  void initialize(int width, int height) {
    _width = width;
    _height = height;

    glGenFramebuffers(1, &_fbo);
    bind();

    _color.initialize(width, height);
    _depth.initialize(width, height);

    attachTextures();

    // @TODO: Only run in debug mode
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      std::cout << "Framebuffer not complete!" << std::endl;
    }

    unbind();
  }

  void bind(GLenum target = GL_FRAMEBUFFER) const {
    glBindFramebuffer(target, _fbo);
  }

  void unbind(GLenum target = GL_FRAMEBUFFER) const {
    glBindFramebuffer(target, 0);
  }

  const Texture2D& getColorTexture() const { return _color; }

  const DepthBuffer& getDepthBuffer() const { return _depth; }

  void resize(int newWidth, int newHeight) {
    if (newWidth == _width && newHeight == _height) {
      return;
    }

    _width = newWidth;
    _height = newHeight;

    _color.resize(_width, _height);
    _depth.resize(_width, _height);

    bind(GL_FRAMEBUFFER);
    attachTextures();

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      std::cerr << "Framebuffer not complete after resize!" << std::endl;
    }

    unbind(GL_FRAMEBUFFER);
  }

  void clear(float r, float g, float b, float a, bool clearDepth = true) const {
    bind(GL_FRAMEBUFFER);
    glClearColor(r, g, b, a);
    GLbitfield bits = GL_COLOR_BUFFER_BIT | (clearDepth ? GL_DEPTH_BUFFER_BIT : 0);
    glClear(bits);
  }

  GLuint id() const { return _fbo; }
  bool isValid() const { return _fbo != 0; }

  const Texture2D& color() const { return _color; }
  Texture2D& color() { return _color; }

  int width() const { return _width; }
  int height() const { return _height; }

  bool hasDepth() const { return _depth.isValid(); }

 private:
  GLuint _fbo = 0;
  Texture2D _color;
  DepthBuffer _depth;
  int _width = 0;
  int _height = 0;

  void attachTextures() {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _color.id(), 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, _depth.id());
  }
};

}  // namespace gl