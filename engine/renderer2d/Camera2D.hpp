#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "gl/GLBuffer.hpp"

class Camera2D {
 public:
  // std140 layout for Camera2D UBO
  struct CameraBlock {
    glm::mat4 proj;
    glm::mat4 view;
    glm::mat4 projView;
    glm::vec4 viewport;  // [u, v, _, _]
  };

  // UBO binding point used in the shaders
  static constexpr GLuint bindingPoint = 0;

  Camera2D() = default;
  ~Camera2D() = default;

  Camera2D(const Camera2D&) noexcept = delete;
  Camera2D& operator=(const Camera2D&) noexcept = delete;
  Camera2D(Camera2D&& other) noexcept = delete;
  Camera2D& operator=(Camera2D&& other) noexcept = delete;

  void initialize(int viewportW, int viewportH) {
    _vpW = viewportW;
    _vpH = viewportH;
    _ubo.initialize(gl::BufferType::Uniform, gl::BufferUsage::DynamicDraw);
    _dirty = true;
    upload();
  }

  void setViewport(int w, int h) {
    _vpW = w;
    _vpH = h;
    _dirty = true;
  }
  void setPosition(glm::vec2 p) {
    _pos = p;
    _dirty = true;
  }
  void setRotationRad(float r) {
    _rot = r;
    _dirty = true;
  }
  void setZoom(float z) {
    _zoom = (z > 0.f ? z : 1.f);
    _dirty = true;
  }

  glm::vec2 position() const { return _pos; }
  float rotationRad() const { return _rot; }
  float zoom() const { return _zoom; }
  const glm::mat4& proj() const { return _proj; }
  const glm::mat4& view() const { return _view; }
  const glm::mat4& projView() const { return _projView; }
  GLuint uboId() const { return _ubo.id(); }

  void upload() {
    if (_dirty) {
      rebuildMatrices();
    }

    _ubo.bind();

    CameraBlock block;
    block.proj = _proj;
    block.view = _view;
    block.projView = _projView;
    block.viewport = glm::vec4{static_cast<float>(_vpW), static_cast<float>(_vpH), 0.0f, 0.0f};

    _ubo.setData(&block, sizeof(block));
    glBindBufferBase(GL_UNIFORM_BUFFER, bindingPoint, _ubo.id());
  }

 private:
  gl::GLBuffer _ubo;

  // State
  int _vpW = 1;
  int _vpH = 1;
  glm::vec2 _pos{0.f, 0.f};
  float _rot = 0.f;   // radians
  float _zoom = 1.f;  // >1 zooms in

  // Cached matrices
  glm::mat4 _proj{1.f};
  glm::mat4 _view{1.f};
  glm::mat4 _projView{1.f};
  bool _dirty = true;

  void rebuildMatrices() {
    // normalize by zoom
    const float halfW = 0.5f * static_cast<float>(_vpW);
    const float halfH = 0.5f * static_cast<float>(_vpH);
    const float zx = 1.0f / _zoom;
    const float zy = 1.0f / _zoom;

    // convert to world coordinates
    const float left = _pos.x - halfW * zx;
    const float right = _pos.x + halfW * zx;
    const float bottom = _pos.y - halfH * zy;
    const float top = _pos.y + halfH * zy;

    _proj = glm::ortho(left, right, bottom, top, -1.0f, 1.0f);

    const glm::mat4 T = glm::translate(glm::mat4(1.f), glm::vec3(-_pos, 0.f));
    const glm::mat4 R = glm::rotate(glm::mat4(1.f), -_rot, glm::vec3(0, 0, 1));
    _view = R * T;

    _projView = _proj * _view;
    _dirty = false;
  }
};