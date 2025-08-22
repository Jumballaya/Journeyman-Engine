#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../SpriteInstance.hpp"
#include "../common.hpp"
#include "GLBuffer.hpp"

namespace gl {

// @TODO: Create a VertexInstanceLayout struct and then
//        create a meta VertexArrayLayout struct to wrap both.
//
//
// @TODO: Switch to DSA since we are using OGL 4.6
//
struct VertexLayout {
  int elementCount;
  GLenum type;
  bool normalized;
  size_t offset;
};

struct VertexArray {
 public:
  VertexArray() = default;

  ~VertexArray() {
    if (isValid()) {
      glDeleteVertexArrays(1, &_vao);
    }
  }

  VertexArray(VertexArray&) noexcept = delete;
  VertexArray& operator=(VertexArray&) noexcept = delete;

  VertexArray(VertexArray&& other) noexcept
      : _vao(other._vao), _vbo(std::move(other._vbo)), _ibo(std::move(other._ibo)), _instanceBuffer(std::move(other._instanceBuffer)), _isIndexed(other._isIndexed), _isInstanced(other._isInstanced) {
    other._vao = 0;
  }

  VertexArray& operator=(VertexArray&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    _vao = other._vao;
    _ibo = std::move(other._ibo);
    _vbo = std::move(other._vbo);
    _instanceBuffer = std::move(other._instanceBuffer);
    _isIndexed = other._isIndexed;
    _isInstanced = other._isInstanced;
    other._vao = 0;
    return *this;
  }

  void initialize(bool isInstanced = false) {
    glGenVertexArrays(1, &_vao);
    bind();
    _vbo.initialize(gl::BufferType::Vertex);
    _ibo.initialize(gl::BufferType::Index);
    _instanceBuffer.initialize(gl::BufferType::Instance, gl::BufferUsage::DynamicDraw);
    _isInstanced = isInstanced;
  }

  void setIndexData(std::span<const uint16_t> data) {
    bind();
    _ibo.setData(data.data(), data.size() * sizeof(uint16_t));
    _isIndexed = true;
    _indexCount = data.size();
  }

  void setVertexData(std::span<const float> data, int strideBytes, std::span<const VertexLayout> layout) {
    bind();
    _vbo.setData(data.data(), data.size_bytes());

    for (int i = 0; i < layout.size(); ++i) {
      const auto& vl = layout[i];
      glEnableVertexAttribArray(i);
      glVertexAttribPointer(i, vl.elementCount, vl.type, vl.normalized, strideBytes, reinterpret_cast<void*>(vl.offset));
    }

    const int floatsPerVertex = strideBytes / static_cast<int>(sizeof(float));
    _vertexCount = static_cast<GLsizei>(data.size() / floatsPerVertex);
  }

  // @TODO: Split this into an initializeInstanceData and a setInstanceData
  //        initialize will take a VertexInstanceLayout and build the code bellow
  //        setInstanceData will just set the data on the buffer
  void setInstanceData(const void* data, size_t size, int stride) const {
    bind();
    // Hard code sprite instance for now until I create a VertexInstanceLayout struct
    // start at '2' because 0 is a_position and 1 is a_texCoord
    int base = 2;
    _instanceBuffer.bind();

    // Transform
    for (int i = 0; i < 4; ++i) {
      glEnableVertexAttribArray(base + i);
      glVertexAttribPointer(base + i, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteInstance), (void*)(sizeof(float) * i * 4));
      glVertexAttribDivisor(base + i, 1);
    }

    // Color
    glEnableVertexAttribArray(base + 4);
    glVertexAttribPointer(base + 4, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteInstance), (void*)(sizeof(float) * 16));
    glVertexAttribDivisor(base + 4, 1);

    // TexRect
    glEnableVertexAttribArray(base + 5);
    glVertexAttribPointer(base + 5, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteInstance), (void*)(sizeof(float) * 20));
    glVertexAttribDivisor(base + 5, 1);

    // Layer
    glEnableVertexAttribArray(base + 6);
    glVertexAttribPointer(base + 6, 1, GL_FLOAT, GL_FALSE, sizeof(SpriteInstance), (void*)(sizeof(float) * 24));
    glVertexAttribDivisor(base + 6, 1);

    _instanceBuffer.setData(data, size);
  }

  bool isValid() const {
    return _vao != 0;
  }

  void bind() const {
    if (_vao) {
      glBindVertexArray(_vao);
      return;
    }
    unbind();
  }

  void unbind() const {
    glBindVertexArray(0);
  }

  void draw(size_t instanceCount = 1) const {
    bind();
    if (_isIndexed && !_isInstanced) {
      glDrawElements(GL_TRIANGLES, _indexCount, GL_UNSIGNED_SHORT, nullptr);
      return;
    }
    if (_isIndexed && _isInstanced) {
      glDrawElementsInstanced(GL_TRIANGLES, _indexCount, GL_UNSIGNED_SHORT, nullptr, instanceCount);
      return;
    }
    if (!_isIndexed && _isInstanced) {
      glDrawArraysInstanced(GL_TRIANGLES, 0, _vertexCount, instanceCount);
      return;
    }
    glDrawArrays(GL_TRIANGLES, 0, _vertexCount);
  }

 private:
  GLuint _vao;
  GLBuffer _vbo;
  GLBuffer _ibo;
  GLBuffer _instanceBuffer;

  size_t _vertexCount;
  size_t _indexCount;

  bool _isIndexed = false;
  bool _isInstanced = false;
};
}  // namespace gl