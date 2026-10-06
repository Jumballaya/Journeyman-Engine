#pragma once

#include <span>

#include "../common.hpp"
#include "GLBuffer.hpp"

namespace gl {

// A VAO over interleaved vertices: position (xyz) at attribute 0, uv at 1.
struct VertexArray {
  VertexArray() = default;
  ~VertexArray() { destroy(); }
  VertexArray(const VertexArray&) = delete;
  VertexArray& operator=(const VertexArray&) = delete;

  // Leaves the VAO bound, so callers can add index or instance buffers to it.
  void initialize(std::span<const float> xyzuv) {
    glGenVertexArrays(1, &_vao);
    bind();
    _vertices.initialize(GL_ARRAY_BUFFER);
    _vertices.setData(xyzuv.data(), xyzuv.size_bytes());
    constexpr GLsizei stride = 5 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
  }

  void bind() const { glBindVertexArray(_vao); }

  void destroy() {
    if (_vao) glDeleteVertexArrays(1, &_vao);
    _vao = 0;
    _vertices.destroy();
  }

 private:
  GLuint _vao = 0;
  GLBuffer _vertices;
};

}  // namespace gl
