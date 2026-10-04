#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "../core/ecs/component/Component.hpp"
#include "TextureHandle.hpp"

// Named flipbook animations for the entity's SpriteComponent, with frames
// resolved to textures at load.
struct SpriteAnimationComponent : public Component<SpriteAnimationComponent> {
  COMPONENT_NAME("SpriteAnimationComponent");

  struct Frame {
    TextureHandle texture;
    glm::vec4 texRect;
  };
  struct Animation {
    std::vector<Frame> frames;
    float frameDuration = 0.1f;  // seconds
    bool loop = true;            // false: hold the last frame and report finished
  };

  std::unordered_map<std::string, Animation> animations;
  std::string current;  // "" = not playing
  float elapsed = 0.0f;
  uint32_t frame = 0;
  bool finished = false;

  // Restarts `name` from its first frame; false if there is no such animation.
  bool play(const std::string& name) {
    if (!animations.contains(name)) return false;
    current = name;
    elapsed = 0.0f;
    frame = 0;
    finished = false;
    return true;
  }
};
