#pragma once

#include "../common.hpp"

namespace gl {

struct DepthBuffer {
  DepthBuffer() = default;

  ~DepthBuffer() {
    if (isValid()) {
      glDeleteRenderbuffers(1, &_depth);
    }
  }

  DepthBuffer(const DepthBuffer&) noexcept = delete;
  DepthBuffer& operator=(const DepthBuffer&) noexcept = delete;

  DepthBuffer(DepthBuffer&& other) noexcept
      : _width(other._width), _height(other._height), _depth(other._depth) {
    other._width = 0;
    other._height = 0;
    other._depth = 0;
  }

  DepthBuffer& operator=(DepthBuffer&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    if (isValid()) {
      glDeleteRenderbuffers(1, &_depth);
    }

    _width = other._width;
    _height = other._height;
    _depth = other._depth;

    other._width = 0;
    other._height = 0;
    other._depth = 0;

    return *this;
  }

  void initialize(int width, int height) {
    _width = width;
    _height = height;

    glGenRenderbuffers(1, &_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, _depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, _width, _height);
  }

  void resize(int newWidth, int newHeight) {
    if (_width == newWidth && _height == newHeight) {
      return;
    }
    if (isValid()) {
      glDeleteRenderbuffers(1, &_depth);
    }

    initialize(newWidth, newHeight);
  }

  GLuint id() const {
    return _depth;
  }

  bool isValid() const {
    return _depth != 0;
  }

 private:
  GLuint _depth = 0;
  GLsizei _width = 0;
  GLsizei _height = 0;
};
};  // namespace gl