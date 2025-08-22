#include "GLBuffer.hpp"

namespace gl {
GLBuffer::~GLBuffer() {
  if (isValid()) {
    glDeleteBuffers(1, &_id);
  }
}

GLBuffer::GLBuffer(GLBuffer&& other) noexcept
    : _id(other._id), _target(other._target), _usage(other._usage) {
  other._id = 0;
}

GLBuffer& GLBuffer::operator=(GLBuffer&& other) noexcept {
  if (this == &other) {
    return *this;
  }

  _id = other._id;
  _target = other._target;
  _usage = other._usage;
  other._id = 0;

  return *this;
}

void GLBuffer::initialize(BufferType type, BufferUsage usage) {
  _target = toGLTarget(type);
  _usage = toGLUsage(usage);
  glGenBuffers(1, &_id);
}

void GLBuffer::bind() const {
  glBindBuffer(_target, _id);
}

void GLBuffer::unbind() const {
  glBindBuffer(_target, 0);
}

void GLBuffer::setData(const void* data, size_t size) const {
  bind();
  glBufferData(_target, size, data, _usage);
}

GLuint GLBuffer::id() const {
  return _id;
}

bool GLBuffer::isValid() const {
  return _id != 0;
}

}  // namespace gl
