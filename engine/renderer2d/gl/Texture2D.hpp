#pragma once

#include <utility>

#include "../common.hpp"

namespace gl {

// An RGBA8 texture, nearest-filtered and clamped to its edges by default.
struct Texture2D {
  Texture2D() = default;
  ~Texture2D() { destroy(); }
  Texture2D(const Texture2D&) = delete;
  Texture2D& operator=(const Texture2D&) = delete;
  Texture2D(Texture2D&& other) noexcept { *this = std::move(other); }
  // Swaps, so the moved-from texture frees what this one held.
  Texture2D& operator=(Texture2D&& other) noexcept {
    std::swap(_texture, other._texture);
    std::swap(_width, other._width);
    std::swap(_height, other._height);
    return *this;
  }

  // (Re)allocates the texture with undefined contents; leaves it bound.
  void initialize(int width, int height) {
    destroy();
    _width = width;
    _height = height;
    glGenTextures(1, &_texture);
    bind();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    setFilter(GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  }

  // Uploads w*h RGBA8 pixels at (x, y); the rect must fit inside the texture.
  void subUpload(int x, int y, int w, int h, const void* pixels) {
    bind();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  }

  void setFilter(GLenum filter) {
    bind();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  }

  void bind() { glBindTexture(GL_TEXTURE_2D, _texture); }
  void bindToSlot(GLuint slot) {
    glActiveTexture(GL_TEXTURE0 + slot);
    bind();
  }

  GLuint id() const { return _texture; }
  GLsizei width() const { return _width; }
  GLsizei height() const { return _height; }

  void destroy() {
    if (_texture) glDeleteTextures(1, &_texture);
    _texture = 0;
    _width = _height = 0;
  }

 private:
  GLuint _texture = 0;
  GLsizei _width = 0;
  GLsizei _height = 0;
};

}  // namespace gl
