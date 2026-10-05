#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "../ecs/World.hpp"
#include "../ecs/entity/EntityId.hpp"
#include "GameState.hpp"

// Each entity's script-visible key/value store (entity.data in scripts), made on
// first use and dropped once the entity is gone. Stores are numbered from 2,
// after the session (0) and save (1) stores. Thread-safe.
class EntityStores {
 public:
  static constexpr int32_t kFirstId = 2;

  // The entity's store id, creating the store.
  int32_t idFor(EntityId entity) {
    std::lock_guard lock(_mutex);
    auto [it, added] = _ids.try_emplace(entity, _nextId);
    if (added) _stores[_nextId++] = std::make_unique<GameState>();
    return it->second;
  }

  // Null for unknown or released ids.
  GameState* find(int32_t id) {
    std::lock_guard lock(_mutex);
    auto it = _stores.find(id);
    return it == _stores.end() ? nullptr : it->second.get();
  }

  // Main thread, between frames: releases the stores of destroyed entities.
  void prune(const World& world) {
    std::lock_guard lock(_mutex);
    for (auto it = _ids.begin(); it != _ids.end();) {
      if (world.isAlive(it->first)) {
        ++it;
        continue;
      }
      _stores.erase(it->second);
      it = _ids.erase(it);
    }
  }

 private:
  std::mutex _mutex;
  std::unordered_map<EntityId, int32_t> _ids;
  std::unordered_map<int32_t, std::unique_ptr<GameState>> _stores;
  int32_t _nextId = kFirstId;
};
