#pragma once

#include <algorithm>

#include "../core/ecs/World.hpp"
#include "../core/ecs/system/System.hpp"
#include "SpriteAnimationComponent.hpp"
#include "SpriteComponent.hpp"

// Advances animations (a step's time): SpriteFrameSystem shows the frame.
class SpriteAnimationSystem : public System {
 public:
  void update(World& world, float dt) override {
    for (auto [entity, anim] : world.view<SpriteAnimationComponent>()) {
      auto it = anim->animations.find(anim->current);
      if (it == anim->animations.end() || it->second.frames.empty()) continue;
      const auto& a = it->second;
      const auto count = static_cast<uint32_t>(a.frames.size());

      anim->elapsed += dt;
      while (!anim->finished && anim->elapsed >= a.frames[anim->frame].duration) {
        anim->elapsed -= a.frames[anim->frame].duration;
        if (a.loop) {
          anim->frame = (anim->frame + 1) % count;
        } else if (anim->frame + 1 < count) {
          ++anim->frame;
        }
      }
      if (!a.loop && anim->frame + 1 >= count) anim->finished = true;
    }
  }

  const char* name() const override { return "SpriteAnimationSystem"; }
};

// Shows each animated sprite's current frame as it's drawn: one spawned or
// played since the last step too (and a scene drawn before any step).
class SpriteFrameSystem : public System {
 public:
  void update(World& world, float) override {
    for (auto [entity, anim, sprite] : world.view<SpriteAnimationComponent, SpriteComponent>()) {
      auto it = anim->animations.find(anim->current);
      if (it == anim->animations.end() || it->second.frames.empty()) continue;
      const auto& frame = it->second.frames[std::min<size_t>(anim->frame, it->second.frames.size() - 1)];
      sprite->texture = frame.texture;
      sprite->texRect = frame.texRect;
    }
  }

  const char* name() const override { return "SpriteFrameSystem"; }
};
