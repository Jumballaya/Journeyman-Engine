#pragma once

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "HostedEngine.hpp"
#include "SceneDocument.hpp"

class TileGrid;

// The open scene as the Scene view draws it: an edit-mode engine (nothing
// simulates) whose entities follow a SceneDocument, respawned as they change.
class Preview {
 public:
  // Where an entity sits in the world, for picking, outlines and gizmos.
  struct Bounds {
    std::array<glm::vec2, 4> corners;  // rotated quad, counter-clockwise
    glm::vec2 min, max;                // axis-aligned box around them
    glm::vec2 position;                // the transform's
    float z = 0;
    bool point = false;  // no visual: a transform alone
  };
  struct Collider {
    glm::vec2 center, half;
  };

  // Starts (or restarts, after a build) the engine on the project's build/.
  // False with error() set if it can't.
  bool start(const Project& project);
  void stop();
  bool running() const { return _engine != nullptr; }
  const std::string& error() const { return _error; }

  // Brings the engine's entities in line with `doc` (cheap when nothing changed).
  // `resolve` turns a scene entry into what the engine should spawn.
  void sync(const SceneDocument& doc, const std::function<Json(const Json&)>& resolve);
  // Respawns everything on the next sync (after a rebuild or a prefab change).
  void invalidate() { _spawned.clear(), _syncedPath.clear(); }

  // Renders the scene around `center` at `zoom` into `width` x `height` pixels,
  // `scale` pixels per point. Returns the GL texture.
  unsigned render(glm::vec2 center, float zoom, int width, int height, float scale, float dt);

  std::optional<Bounds> bounds(EntityUid uid) const;
  std::vector<Collider> colliders(EntityUid uid) const;
  // Entities under a world point, topmost first.
  std::vector<EntityUid> pick(glm::vec2 world) const;
  // Entities whose bounds overlap a world rectangle.
  std::vector<EntityUid> pickRect(glm::vec2 a, glm::vec2 b) const;
  // An entity's tile map grid and the world position of its bottom-left; null if none.
  const TileGrid* tileGrid(EntityUid uid, glm::vec2* origin = nullptr) const;
  // Whether spawning the entry failed (the reason in the console).
  bool failed(EntityUid uid) const;
  // Every spawned entity's bounds (for the scene's overall extent).
  std::optional<std::pair<glm::vec2, glm::vec2>> extent() const;
  // The game's logical resolution and clear color, from the manifest.
  glm::ivec2 gameSize() const { return _gameSize; }

  HostedEngine* engine() { return _engine.get(); }

 private:
  struct Spawned {
    EntityId id;
    Json json;  // what it was spawned from
    bool failed = false;
  };
  std::unique_ptr<HostedEngine> _engine;
  std::string _error;
  std::string _syncedPath;
  uint64_t _syncedRevision = ~0ull;
  std::map<EntityUid, Spawned> _spawned;
  glm::ivec2 _gameSize{0};
};
