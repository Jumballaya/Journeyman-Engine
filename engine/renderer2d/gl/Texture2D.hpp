#pragma once

#include <cstddef>
#include <cstdint>

#include "../common.hpp"

namespace gl {

struct Texture2D {
 public:
  Texture2D() = default;
  ~Texture2D() {
    if (isValid()) {
      glDeleteTextures(1, &_texture);
    }
  }

  Texture2D(const Texture2D&) noexcept = delete;
  Texture2D& operator=(const Texture2D&) noexcept = delete;

  Texture2D(Texture2D&& other) noexcept
      : _texture(other._texture), _width(other._width), _height(other._height), _internalFormat(other._internalFormat) {
    other._texture = 0;
    other._width = 0;
    other._height = 0;
    other._internalFormat = GL_RGBA8;
  }

  Texture2D& operator=(Texture2D&& other) noexcept {
    if (this == &other) return *this;
    if (isValid()) {
      glDeleteTextures(1, &_texture);
    }
    _texture = other._texture;
    _width = other._width;
    _height = other._height;
    _internalFormat = other._internalFormat;
    other._texture = 0;
    other._width = 0;
    other._height = 0;
    other._internalFormat = GL_RGBA8;
    return *this;
  }

  void initialize(GLsizei width, GLsizei height, GLenum internalFormat = GL_RGBA8, GLenum format = GL_RGBA, GLenum type = GL_UNSIGNED_BYTE) {
    glGenTextures(1, &_texture);
    _width = width;
    _height = height;
    _internalFormat = internalFormat;
    _format = format;
    _type = type;

    bind();

    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, _format, _type, nullptr);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  void setData(void* data) {
    bind();
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, _width, _height, _format, _type, data);
  }

  void bindToSlot(GLuint slot) {
    glActiveTexture(GL_TEXTURE0 + slot);
    bind();
  }

  void bind() {
    glBindTexture(GL_TEXTURE_2D, _texture);
  }

  bool isValid() const {
    return _texture != 0;
  }

  GLuint id() const {
    return _texture;
  }

  void resize(int newWidth, int newHeight) {
    if (_width == newWidth && _height == newHeight) {
      return;
    }

    _width = newWidth;
    _height = newHeight;

    if (isValid()) {
      glDeleteTextures(1, &_texture);
    }

    glGenTextures(1, &_texture);
    bind();
    initialize(newWidth, newHeight, _internalFormat, _format, _type);
  }

 private:
  GLuint _texture = 0;
  GLsizei _width = 0;
  GLsizei _height = 0;
  GLenum _internalFormat = GL_RGBA8;
  GLenum _format = GL_RGBA;
  GLenum _type = GL_UNSIGNED_BYTE;
};

}  // namespace gl