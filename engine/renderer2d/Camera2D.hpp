#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// The world camera: `position` is the view's center; zoom > 1 zooms in.
class Camera2D {
 public:
  void setViewport(int width, int height) { _viewport = {width, height}; }
  void setPosition(glm::vec2 position) { _position = position; }
  void setZoom(float zoom) { _zoom = zoom > 0.0f ? zoom : 1.0f; }

  glm::vec2 position() const { return _position; }
  float zoom() const { return _zoom; }

  glm::mat4 projView() const {
    const glm::vec2 half = glm::vec2(_viewport) * 0.5f / _zoom;
    // z is draw order (sorted on the CPU), not depth: keep any sane z unclipped.
    const glm::mat4 proj = glm::ortho(-half.x, half.x, -half.y, half.y, -10000.0f, 10000.0f);
    return glm::translate(proj, glm::vec3(-_position, 0.0f));
  }

 private:
  glm::ivec2 _viewport{1, 1};
  glm::vec2 _position{0.0f};
  float _zoom = 1.0f;
};
