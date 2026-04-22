#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "PostEffect.hpp"
#include "PostEffectHandle.hpp"

class PostEffectChain {
 public:
  PostEffectHandle add(PostEffect effect);

  void remove(PostEffectHandle handle);
  void setEnabled(PostEffectHandle handle, bool enabled);
  void setUniform(PostEffectHandle handle, std::string_view name, UniformValue value);
  void setAuxTexture(PostEffectHandle handle, TextureHandle tex);

  void moveTo(PostEffectHandle handle, size_t newIndex);

  size_t size() const;
  bool contains(PostEffectHandle handle) const;

  const PostEffect* get(PostEffectHandle handle) const;

  std::vector<const PostEffect*> enabledEffects() const;

 private:
  std::vector<PostEffect> _effects;
  uint32_t _nextId = 1;

  std::vector<PostEffect>::iterator findEffect(PostEffectHandle handle);
  std::vector<PostEffect>::const_iterator findEffect(PostEffectHandle handle) const;
};
