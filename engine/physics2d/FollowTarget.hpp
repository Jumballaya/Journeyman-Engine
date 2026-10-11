#pragma once

#include <algorithm>
#include <string>

#include "../core/ecs/World.hpp"

// The entity something follows: one a script chose, else the first named
// `name` (found again when that one is gone: a respawned player).
struct FollowTarget {
  std::string name;
  EntityId chosen = kNoEntityId;

  EntityId resolve(World& world) {
    if (world.isAlive(chosen) || name.empty()) return chosen;
    const auto named = world.findWithTag(name);
    chosen = named.empty() ? kNoEntityId
                           : *std::min_element(named.begin(), named.end(), [](EntityId a, EntityId b) { return a.index < b.index; });
    return chosen;
  }
  // A script's choice: no falling back to the authored name (kNoEntityId: none).
  void choose(EntityId entity) {
    name.clear();
    chosen = entity;
  }
};
