#pragma once

#include "../core/ecs/World.hpp"
#include "../core/ecs/system/System.hpp"
#include "SpriteAnimationComponent.hpp"
#include "SpriteComponent.hpp"

// Advances animations and writes the current frame into the SpriteComponent.
class SpriteAnimationSystem : public System {
 public:
  void update(World& world, float dt) override {
    for (auto [entity, anim, sprite] : world.view<SpriteAnimationComponent, SpriteComponent>()) {
      auto it = anim->animations.find(anim->current);
      if (it == anim->animations.end() || it->second.frames.empty()) continue;
      const auto& a = it->second;
      const auto count = static_cast<uint32_t>(a.frames.size());

      anim->elapsed += dt;
      while (!anim->finished && anim->elapsed >= a.frameDuration) {
        anim->elapsed -= a.frameDuration;
        if (a.loop) {
          anim->frame = (anim->frame + 1) % count;
        } else if (anim->frame + 1 < count) {
          ++anim->frame;
        }
      }
      if (!a.loop && anim->frame + 1 >= count) anim->finished = true;

      sprite->texture = a.frames[anim->frame].texture;
      sprite->texRect = a.frames[anim->frame].texRect;
    }
  }

  const char* name() const override { return "SpriteAnimationSystem"; }
};
