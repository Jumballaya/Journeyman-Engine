#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../common.hpp"

namespace gl {

struct Texture2DArray {
 public:
  Texture2DArray() = default;

  ~Texture2DArray() {
    if (isValid()) {
      glDeleteTextures(1, &_texture);
    }
  }

  Texture2DArray(const Texture2DArray&) = delete;
  Texture2DArray& operator=(const Texture2DArray&) = delete;

  Texture2DArray(Texture2DArray&& other) noexcept
      : _texture(other._texture), _width(other._width), _height(other._height), _layers(other._layers), _internalFormat(other._internalFormat) {
    other._texture = 0;
    other._width = 0;
    other._height = 0;
    other._layers = 0;
  }

  Texture2DArray& operator=(Texture2DArray&& other) noexcept {
    if (this == &other) return *this;
    if (isValid()) glDeleteTextures(1, &_texture);
    _texture = other._texture;
    _width = other._width;
    _height = other._height;
    _layers = other._layers;
    _internalFormat = other._internalFormat;
    other._texture = 0;
    other._width = 0;
    other._height = 0;
    other._layers = 0;
    return *this;
  }

  void initialize(GLsizei width, GLsizei height, GLsizei layers, GLint internalFormat = GL_RGBA8) {
    _width = width;
    _height = height;
    _layers = layers;
    _internalFormat = internalFormat;

    glGenTextures(1, &_texture);
    bindToSlot(0);  // temp bind

    glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, internalFormat, width, height, layers);

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  void setLayerData(GLint layerIndex, const std::span<const std::uint8_t> data, GLenum format = GL_RGBA, GLenum type = GL_UNSIGNED_BYTE) {
    assert(isValid());
    assert(layerIndex >= 0 && layerIndex < _layers);
    bindToSlot(0);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layerIndex, _width, _height, 1, format, type, data.data());
  }

  void bindToSlot(GLuint slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D_ARRAY, _texture);
  }

  bool isValid() const {
    return _texture != 0;
  }

 private:
  GLuint _texture = 0;
  GLsizei _width = 0;
  GLsizei _height = 0;
  GLsizei _layers = 0;
  GLint _internalFormat = GL_RGBA8;
};

}  // namespace gl