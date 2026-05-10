#pragma once

#include <cstdint>
#include <string>

#include "../core/ecs/World.hpp"
#include "../core/ecs/system/System.hpp"
#include "../core/logger/logging.hpp"
#include "AtlasManager.hpp"
#include "SpriteAnimationComponent.hpp"
#include "SpriteComponent.hpp"

// Advances each animated sprite's playhead and writes the current frame's
// (texture, texRect) into the matching SpriteComponent. Runs BEFORE
// Renderer2DSystem so the renderer reads up-to-date values within the same
// frame. The system is stateless — all per-entity state lives on the
// component.
class SpriteAnimationSystem : public System {
 public:
  explicit SpriteAnimationSystem(AtlasManager& atlasManager)
      : _atlasManager(&atlasManager) {}

  ~SpriteAnimationSystem() override = default;

  void update(World& world, float dt) override {
    if (_atlasManager == nullptr) {
      JM_LOG_CRITICAL("AtlasManager was not passed to SpriteAnimationSystem");
      return;
    }

    for (auto [entity, anim, sprite] :
         world.view<SpriteAnimationComponent, SpriteComponent>()) {
      if (anim->current.empty()) {
        continue;
      }

      auto it = anim->animations.find(anim->current);
      if (it == anim->animations.end() || it->second.regions.empty()) {
        continue;
      }
      auto& a = it->second;

      // Edge: a non-looping animation with a single frame is "instantly
      // finished" the first time the system sees it (the advance loop below
      // never enters because there's no next frame to clamp to).
      if (!a.loop && a.regions.size() == 1 && !anim->_finished) {
        anim->_finished = true;
      }

      anim->elapsed += dt;
      while (anim->elapsed >= a.frameDuration) {
        anim->elapsed -= a.frameDuration;
        if (a.loop) {
          anim->frameIndex = (anim->frameIndex + 1) %
                             static_cast<uint32_t>(a.regions.size());
        } else if (anim->frameIndex + 1 <
                   static_cast<uint32_t>(a.regions.size())) {
          ++anim->frameIndex;
        } else {
          anim->elapsed = 0.0f;
          anim->_finished = true;
          break;
        }
      }

      const std::string& regionName = a.regions[anim->frameIndex];
      auto resolved = _atlasManager->lookup(anim->_atlasHandle, regionName);
      if (!resolved.has_value()) {
        const std::string key = anim->current + ":" + regionName;
        if (anim->_warned.insert(key).second) {
          JM_LOG_WARN(
              "[SpriteAnimation] frame region '{}' not found in atlas (animation '{}')",
              regionName, anim->current);
        }
        continue;
      }
      sprite->texture = resolved->first;
      sprite->texRect = resolved->second;
    }
  }

  const char* name() const override { return "SpriteAnimationSystem"; }

 private:
  AtlasManager* _atlasManager = nullptr;
};
