#include "PostEffectChain.hpp"

#include <algorithm>
#include <string>
#include <utility>

PostEffectHandle PostEffectChain::add(PostEffect effect) {
  effect.handle = PostEffectHandle{_nextId++};
  _effects.push_back(std::move(effect));
  return _effects.back().handle;
}

void PostEffectChain::remove(PostEffectHandle handle) {
  std::erase_if(_effects, [handle](const PostEffect& e) { return e.handle == handle; });
}

void PostEffectChain::setEnabled(PostEffectHandle handle, bool enabled) {
  if (PostEffect* effect = find(handle)) effect->enabled = enabled;
}

void PostEffectChain::setUniform(PostEffectHandle handle, std::string_view name, UniformValue value) {
  if (PostEffect* effect = find(handle)) effect->uniforms[std::string(name)] = value;
}

const PostEffect* PostEffectChain::get(PostEffectHandle handle) const {
  auto it = std::find_if(_effects.begin(), _effects.end(), [handle](const PostEffect& e) { return e.handle == handle; });
  return it == _effects.end() ? nullptr : &*it;
}

std::vector<const PostEffect*> PostEffectChain::enabledEffects() const {
  std::vector<const PostEffect*> out;
  for (const PostEffect& effect : _effects) {
    if (effect.enabled) out.push_back(&effect);
  }
  return out;
}
