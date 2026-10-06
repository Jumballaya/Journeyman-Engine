#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "PostEffect.hpp"

// The effects drawn over each frame, in the order added. Calls with an unknown
// or removed handle do nothing.
class PostEffectChain {
 public:
  // Assigns the effect a new handle (any it carries is ignored).
  PostEffectHandle add(PostEffect effect);
  void remove(PostEffectHandle handle);
  void setEnabled(PostEffectHandle handle, bool enabled);
  void setUniform(PostEffectHandle handle, std::string_view name, UniformValue value);

  const PostEffect* get(PostEffectHandle handle) const;  // nullptr if unknown
  bool contains(PostEffectHandle handle) const { return get(handle) != nullptr; }
  size_t size() const { return _effects.size(); }
  std::vector<const PostEffect*> enabledEffects() const;
  void clear() { _effects.clear(); }

 private:
  std::vector<PostEffect> _effects;
  uint32_t _nextId = 1;

  PostEffect* find(PostEffectHandle handle) { return const_cast<PostEffect*>(get(handle)); }
};
