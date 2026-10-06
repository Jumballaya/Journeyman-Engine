#pragma once

#include <cstddef>

#include "../common.hpp"

namespace gl {

// A GL buffer object for one target (GL_ARRAY_BUFFER, GL_ELEMENT_ARRAY_BUFFER).
struct GLBuffer {
  GLBuffer() = default;
  ~GLBuffer() { destroy(); }
  GLBuffer(const GLBuffer&) = delete;
  GLBuffer& operator=(const GLBuffer&) = delete;

  void initialize(GLenum target, GLenum usage = GL_STATIC_DRAW) {
    _target = target;
    _usage = usage;
    glGenBuffers(1, &_id);
  }

  void bind() const { glBindBuffer(_target, _id); }

  // Binds the buffer and replaces its contents.
  void setData(const void* data, size_t size) const {
    bind();
    glBufferData(_target, static_cast<GLsizeiptr>(size), data, _usage);
  }

  void destroy() {
    if (_id) glDeleteBuffers(1, &_id);
    _id = 0;
  }

 private:
  GLuint _id = 0;
  GLenum _target = 0;
  GLenum _usage = 0;
};

}  // namespace gl
