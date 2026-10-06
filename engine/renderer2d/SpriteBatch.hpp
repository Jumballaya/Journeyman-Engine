#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "SpriteInstance.hpp"
#include "gl/GLBuffer.hpp"
#include "gl/VertexArray.hpp"

// Draws SpriteInstances as instanced quads with whatever texture is bound.
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
    // Per instance, after a_position and a_uv: transform columns (2-5), color (6), texRect (7).
    for (GLuint i = 0; i < 6; ++i) {
      glEnableVertexAttribArray(2 + i);
      glVertexAttribPointer(2 + i, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteInstance),
                            reinterpret_cast<void*>(i * sizeof(glm::vec4)));
      glVertexAttribDivisor(2 + i, 1);
    }
  }

  void submit(const SpriteInstance& instance) { _pending.push_back(instance); }

  // Draws the submitted instances, then forgets them.
  void draw() {
    if (_pending.empty()) return;
    _quad.bind();
    _instances.setData(_pending.data(), _pending.size() * sizeof(SpriteInstance));
    glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr, static_cast<GLsizei>(_pending.size()));
    _pending.clear();
  }

  void destroy() {
    _quad.destroy();
    _indices.destroy();
    _instances.destroy();
    _pending.clear();
  }

 private:
  gl::VertexArray _quad;
  gl::GLBuffer _indices;
  gl::GLBuffer _instances;
  std::vector<SpriteInstance> _pending;
};
