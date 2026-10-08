#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "SpriteInstance.hpp"
#include "gl/GLBuffer.hpp"
#include "gl/VertexArray.hpp"

// Draws SpriteInstances as instanced quads with whatever texture is bound: a
// pass's instances go up in one upload, then each texture run draws its range.
class SpriteBatch {
 public:
  void initialize() {
    static constexpr std::array<float, 20> quad = {
        // x, y, z, u, v
        -1.0f, 1.0f,  0.0f, 0.0f, 1.0f,  // top-left
        -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,  // bottom-left
        1.0f,  -1.0f, 0.0f, 1.0f, 0.0f,  // bottom-right
        1.0f,  1.0f,  0.0f, 1.0f, 1.0f,  // top-right
    };
    static constexpr std::array<uint16_t, 6> indices = {0, 1, 2, 2, 3, 0};
    _quad.initialize(quad);  // still bound: the index buffer and attributes below belong to it
    _indices.initialize(GL_ELEMENT_ARRAY_BUFFER);
    _indices.setData(indices.data(), sizeof(indices));
    _instances.initialize(GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW);
    _instances.bind();
    for (GLuint i = 0; i < 6; ++i) {
      glEnableVertexAttribArray(2 + i);
      glVertexAttribDivisor(2 + i, 1);
    }
    pointInstancesAt(0);
  }

  // Replaces the instance buffer's contents: one upload for a whole pass.
  void upload(std::span<const SpriteInstance> instances) {
    if (!instances.empty()) _instances.setData(instances.data(), instances.size_bytes());
  }

  // Draws `count` uploaded instances starting at `first`, with the bound texture.
  void draw(size_t first, size_t count) {
    if (count == 0) return;
    _quad.bind();
    pointInstancesAt(first);
    glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr, static_cast<GLsizei>(count));
  }

  void destroy() {
    _quad.destroy();
    _indices.destroy();
    _instances.destroy();
  }

 private:
  gl::VertexArray _quad;
  gl::GLBuffer _indices;
  gl::GLBuffer _instances;

  // Per instance, after a_position and a_uv: transform columns (2-5), color (6),
  // texRect (7), read from instance `first` on. (GL 4.1 has no base-instance
  // draw, so a range starts by moving the attributes' offsets.)
  void pointInstancesAt(size_t first) {
    _instances.bind();
    const size_t base = first * sizeof(SpriteInstance);
    for (GLuint i = 0; i < 6; ++i) {
      glVertexAttribPointer(2 + i, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteInstance),
                            reinterpret_cast<void*>(base + i * sizeof(glm::vec4)));
    }
  }
};
