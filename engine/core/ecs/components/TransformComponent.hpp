#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../component/Component.hpp"

struct TransformComponent : public Component<TransformComponent> {
  COMPONENT_NAME("TransformComponent");

  glm::vec3 position{0.0f, 0.0f, 0.f};
  glm::vec2 scale{1.0f, 1.0f};
  float rotationRad = 0.0f;

  glm::mat4 toMatrix() const {
    glm::mat4 T = glm::translate(glm::mat4(1.0f), position);
    glm::mat4 R = glm::rotate(glm::mat4(1.0f), rotationRad, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 S = glm::scale(glm::mat4(1.0f), glm::vec3(scale, 1.0f));
    return T * R * S;
  }
};