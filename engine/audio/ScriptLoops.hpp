#pragma once

#include <functional>
#include <unordered_map>
#include <vector>

#include "../core/ecs/entity/EntityId.hpp"
#include "Voice.hpp"

// The looping sounds scripts started, by the entity whose script started each:
// a hot-reloaded script's old loops stop, fading ones too.
class ScriptLoops {
 public:
  void started(SoundInstanceId id, EntityId owner) { _owners[id] = owner; }
  void stopped(SoundInstanceId id) { _owners.erase(id); }  // not a loop: no-op
  size_t size() const { return _owners.size(); }

  // Lets go of the loops `held` says no (ended, or their owner is gone).
  void keepOnly(const std::function<bool(SoundInstanceId, EntityId)>& held) {
    std::erase_if(_owners, [&](const auto& entry) { return !held(entry.first, entry.second); });
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
