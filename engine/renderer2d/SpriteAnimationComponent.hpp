#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../core/assets/AssetHandle.hpp"
#include "../core/ecs/component/Component.hpp"

// Per-entity animation state. The atlas containing the regions is identified
// by source path at authoring time; the deserializer resolves the path to an
// AssetHandle once and caches it in `_atlasHandle` for hot-path lookups.
//
// All animations on a single component must reference regions in the same
// atlas. Spanning multiple atlases is deferred (F.next).
struct SpriteAnimationComponent : public Component<SpriteAnimationComponent> {
  COMPONENT_NAME("SpriteAnimationComponent");

  struct Animation {
    std::vector<std::string> regions;  // ordered atlas region names
    float frameDuration{0.1f};         // seconds per frame, uniform
    bool loop{true};                   // true = wrap, false = clamp at last
  };

  std::unordered_map<std::string, Animation> animations;
  std::string current;          // currently-playing animation name; "" = none
  float elapsed{0.0f};           // seconds accumulated against frameDuration
  uint32_t frameIndex{0};        // index into animations[current].regions
  std::string atlasPath;         // source path to the atlas (canonicalized)

  // Runtime-only — populated by deserializer, NOT serialized.
  AssetHandle _atlasHandle{};

  // Runtime-only — log de-dup. Key = "<anim>:<region>". Not serialized.
  std::unordered_set<std::string> _warned;

  // Runtime-only — set by SpriteAnimationSystem when a non-looping animation
  // reaches its last frame and the playhead clamps there. Reset to false on
  // setAnimation(). Not serialized.
  bool _finished{false};
};

// POD form is intentionally minimal: only the playback cursor is round-tripped
// across the prefab/scene-state binary boundary. The animations map and the
// atlas path are JSON-only — they're authored content, not runtime mutations.
struct PODSpriteAnimationComponent {
  float elapsed;
  uint32_t frameIndex;
  // `current` is variable-length; the POD layer carries a fixed-size byte
  // slot. 64 chars covers any sane animation name; longer names are truncated
  // (warning logged at serialize time).
  char current[64];
};
