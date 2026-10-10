#pragma once

#include <functional>
#include <unordered_map>
#include <vector>

#include "../core/ecs/entity/EntityId.hpp"
#include "Voice.hpp"

// The looping sounds scripts started that are still playing, by the entity whose
// script started each: a hot-reloaded script's old loops stop.
class ScriptLoops {
 public:
  void started(SoundInstanceId id, EntityId owner) { _owners[id] = owner; }
  void ended(SoundInstanceId id) { _owners.erase(id); }  // stopped or fading out; not a loop: no-op
  void clear() { _owners.clear(); }                       // every sound stopped
  size_t size() const { return _owners.size(); }
  // Lets go of loops whose owner is gone (they play on, as before, untracked).
  void forgetOwnersNot(const std::function<bool(EntityId)>& alive) {
    std::erase_if(_owners, [&](const auto& entry) { return !alive(entry.second); });
  }

  // The loops `owner` started, no longer tracked.
  std::vector<SoundInstanceId> take(EntityId owner) {
    std::vector<SoundInstanceId> taken;
    for (auto it = _owners.begin(); it != _owners.end();) {
      if (it->second != owner) { ++it; continue; }
      taken.push_back(it->first);
      it = _owners.erase(it);
    }
    return taken;
  }

 private:
  std::unordered_map<SoundInstanceId, EntityId> _owners;
};
