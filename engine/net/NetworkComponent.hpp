#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../core/ecs/component/Component.hpp"
#include "../core/ecs/entity/EntityId.hpp"

// Marks an entity as shared by everyone in a multiplayer session. One
// process simulates it (runs its script, owns its state) and streams the
// replicated components' script fields, its tags and its entity.data to the
// others, whose copies follow (interpolated). Format: docs/networking.md.
struct NetworkComponent : Component<NetworkComponent> {
  COMPONENT_NAME("NetworkComponent");

  static constexpr int32_t kHost = -1;    // owner: whoever hosts the session (the server)
  static constexpr int32_t kNobody = -1;  // controller: no player

  // Authored.
  // "authority": "host" (default): the host simulates it, also for entities
  // spawned for a player (Net.spawnPlayer), who then controls it by sending
  // input. "owner": the player it's spawned for simulates it (no input lag,
  // trusted client).
  bool ownerSimulates = false;
  // Components whose script fields are kept in sync.
  std::vector<std::string> replicate{"TransformComponent"};
  // Copies move smoothly between updates (false: they jump to each).
  bool interpolate = true;
  // "scripts": "authority" (default) runs its script only where it's
  // simulated; "everywhere" runs it on every copy too (scripts check me.isMine()).
  bool scriptsEverywhere = false;
  // "ownerLeaves": "destroy" (default) or "host": when its player leaves the
  // host takes it over.
  bool keepWhenOwnerLeaves = false;

  // The session's view of it.
  EntityId entity = kNoEntityId;
  uint32_t netId = 0;  // 0 until the session knows it
  int32_t owner = kHost;
  int32_t controller = kNobody;
  std::string sceneKey;  // built from a scene's entry (SceneLoader::entryKey)
  uint32_t epoch = 0;    // the scene load it belongs to
  bool gone = false;     // gone everywhere: this copy is being destroyed
};
