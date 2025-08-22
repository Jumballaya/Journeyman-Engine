#pragma once

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "SpriteInstance.hpp"
#include "gl/GLBuffer.hpp"
#include "gl/Texture2D.hpp"
#include "gl/VertexArray.hpp"

// QUAD Data
static constexpr std::array<float, 20> vertex_data = {
    // X     Y     Z     U     V
    -1.0f, 1.0f, 0.0f, 0.0f, 1.0f,   // v3: top-left
    -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,  // v0: bottom-left
    1.0f, -1.0f, 0.0f, 1.0f, 0.0f,   // v1: bottom-right
    1.0f, 1.0f, 0.0f, 1.0f, 1.0f,    // v2: top-right
};
static constexpr std::array<uint16_t, 6> index_data = {0, 1, 2, 2, 3, 0};

class SpriteBatch {
 public:
  SpriteBatch() = default;
  ~SpriteBatch() = default;

  SpriteBatch(const SpriteBatch&) noexcept = delete;
  SpriteBatch& operator=(const SpriteBatch&) noexcept = delete;
  SpriteBatch(SpriteBatch&& other) noexcept = delete;
  SpriteBatch& operator=(SpriteBatch&& other) noexcept = delete;

  void initialize() {
    _quadVao.initialize(true);
    std::array<gl::VertexLayout, 2> vertex_layout = {
        gl::VertexLayout{.elementCount = 3,
                         .type = GL_FLOAT,
                         .normalized = false,
                         .offset = 0},
        gl::VertexLayout{.elementCount = 2,
                         .type = GL_FLOAT,
                         .normalized = false,
                         .offset = sizeof(float) * 3}};
    _quadVao.setVertexData(vertex_data, 5 * sizeof(float), vertex_layout);
    _quadVao.setIndexData(index_data);
  }

  void submit(const SpriteInstance& instance) {
    _instances.push_back(instance);
  }

  void setTexture(gl::Texture2D* tex) {
    _boundTexture = tex;
  }

  void flush() {
    _instances.clear();
  }

  void bind() const {
    _quadVao.bind();
  }

  size_t size() const {
    return _instances.size();
  }

  void draw() const {
    if (_boundTexture != nullptr) {
      _boundTexture->bind();
    }
    _quadVao.setInstanceData(_instances.data(), sizeof(SpriteInstance) * _instances.size(), sizeof(SpriteInstance));
    _quadVao.draw(_instances.size());
  }

 private:
  gl::VertexArray _quadVao;
  std::vector<SpriteInstance> _instances;
  gl::Texture2D* _boundTexture = nullptr;
};