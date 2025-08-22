#pragma once

#include "../common.hpp"

namespace gl {

enum class BufferUsage {
  StaticDraw,
  DynamicDraw
};

enum class BufferType {
  Vertex,
  Index,
  Uniform,
  Instance
};

constexpr GLenum toGLTarget(BufferType type) {
  switch (type) {
    case BufferType::Vertex:
      return GL_ARRAY_BUFFER;
    case BufferType::Index:
      return GL_ELEMENT_ARRAY_BUFFER;
    case BufferType::Uniform:
      return GL_UNIFORM_BUFFER;
    case BufferType::Instance:
      return GL_ARRAY_BUFFER;
  }
  return 0;
}

constexpr GLenum toGLUsage(BufferUsage usage) {
  switch (usage) {
    case BufferUsage::StaticDraw:
      return GL_STATIC_DRAW;
    case BufferUsage::DynamicDraw:
      return GL_DYNAMIC_DRAW;
  }
}

struct GLBuffer {
 public:
  GLBuffer() = default;
  ~GLBuffer();

  GLBuffer(const GLBuffer&) = delete;
  GLBuffer& operator=(const GLBuffer&) = delete;
  GLBuffer(GLBuffer&& other) noexcept;
  GLBuffer& operator=(GLBuffer&& other) noexcept;

  void initialize(BufferType type, BufferUsage usage = BufferUsage::StaticDraw);

  void bind() const;
  void unbind() const;
  void setData(const void* data, size_t size) const;
  GLuint id() const;
  bool isValid() const;

 private:
  GLuint _id = 0;
  GLenum _target = 0;
  GLenum _usage = 0;
};

}  // namespace gl