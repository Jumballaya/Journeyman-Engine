#pragma once

#include <utility>

#include "gl/FrameBuffer.hpp"
#include "gl/Texture2D.hpp"

class Surface {
 public:
  Surface() = default;
  ~Surface() = default;

  Surface(const Surface&) noexcept = delete;
  Surface& operator=(const Surface&) noexcept = delete;

  Surface(Surface&& other) noexcept
      : _frameBuffer(std::move(other._frameBuffer)), _width(other._width), _height(other._height) {
    other._width = 0;
    other._height = 0;
  }

  Surface& operator=(Surface&& other) noexcept {
    if (&other == this) return *this;
    _frameBuffer = std::move(other._frameBuffer);
    _width = other._width;
    _height = other._height;
    other._width = 0;
    other._height = 0;
    return *this;
  }

  void initialize(int w, int h) {
    _width = w;
    _height = h;
    _frameBuffer.initialize(w, h);
  }

  void bind() const {
    _frameBuffer.bind();
  }

  void bindRead() const {
    _frameBuffer.bind(GL_READ_FRAMEBUFFER);
  }

  void bindDraw() const {
    _frameBuffer.bind(GL_DRAW_FRAMEBUFFER);
  }

  void unbind() const {
    _frameBuffer.unbind();
  }

  void resize(int w, int h) {
    _width = w;
    _height = h;
    _frameBuffer.resize(w, h);
  }

  void clear(float r, float g, float b, float a, bool clearDepth = true) const {
    _frameBuffer.clear(r, g, b, a, clearDepth);
  }

  int width() const { return _width; }
  int height() const { return _height; }

  gl::Texture2D& color() { return _frameBuffer.color(); }
  const gl::Texture2D& color() const { return _frameBuffer.color(); }

  bool hasDepth() const { return _frameBuffer.hasDepth(); }

 private:
  gl::FrameBuffer _frameBuffer;

  int _width = 0;
  int _height = 0;
};