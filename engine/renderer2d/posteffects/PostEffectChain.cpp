#include "PostEffectChain.hpp"

#include <algorithm>
#include <string>
#include <utility>

PostEffectHandle PostEffectChain::add(PostEffect effect) {
  PostEffectHandle handle{_nextId++};
  effect.handle = handle;
  _effects.push_back(std::move(effect));
  return handle;
}

void PostEffectChain::remove(PostEffectHandle handle) {
  auto it = findEffect(handle);
  if (it == _effects.end()) {
    return;
  }
  _effects.erase(it);
}

void PostEffectChain::setEnabled(PostEffectHandle handle, bool enabled) {
  auto it = findEffect(handle);
  if (it == _effects.end()) {
    return;
  }
  it->enabled = enabled;
}

void PostEffectChain::setUniform(PostEffectHandle handle, std::string_view name, UniformValue value) {
  auto it = findEffect(handle);
  if (it == _effects.end()) {
    return;
  }
  it->uniforms[std::string(name)] = value;
}

void PostEffectChain::setAuxTexture(PostEffectHandle handle, TextureHandle tex) {
  auto it = findEffect(handle);
  if (it == _effects.end()) {
    return;
  }
  it->auxTexture = tex;
}

void PostEffectChain::moveTo(PostEffectHandle handle, size_t newIndex) {
  auto it = findEffect(handle);
  if (it == _effects.end()) {
    return;
  }

  size_t clampedIndex = std::min(newIndex, _effects.size() - 1);
  size_t currentIndex = static_cast<size_t>(std::distance(_effects.begin(), it));
  if (clampedIndex == currentIndex) {
    return;
  }

  PostEffect moved = std::move(*it);
  _effects.erase(it);
  _effects.insert(_effects.begin() + clampedIndex, std::move(moved));
}

size_t PostEffectChain::size() const {
  return _effects.size();
}

bool PostEffectChain::contains(PostEffectHandle handle) const {
  return findEffect(handle) != _effects.end();
}

const PostEffect* PostEffectChain::get(PostEffectHandle handle) const {
  auto it = findEffect(handle);
  if (it == _effects.end()) {
    return nullptr;
  }
  return &(*it);
}

std::vector<const PostEffect*> PostEffectChain::enabledEffects() const {
  std::vector<const PostEffect*> out;
  out.reserve(_effects.size());
  for (const auto& effect : _effects) {
    if (effect.enabled) {
      out.push_back(&effect);
    }
  }
  return out;
}

std::vector<PostEffect>::iterator PostEffectChain::findEffect(PostEffectHandle handle) {
  if (!handle.isValid()) {
    return _effects.end();
  }
  return std::find_if(_effects.begin(), _effects.end(), [handle](const PostEffect& e) {
    return e.handle == handle;
  });
}

std::vector<PostEffect>::const_iterator PostEffectChain::findEffect(PostEffectHandle handle) const {
  if (!handle.isValid()) {
    return _effects.end();
  }
  return std::find_if(_effects.begin(), _effects.end(), [handle](const PostEffect& e) {
    return e.handle == handle;
  });
}
