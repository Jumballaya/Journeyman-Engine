#pragma once

#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "../core/ecs/component/Component.hpp"
#include "../core/ecs/entity/EntityId.hpp"

// A view onto the world, at the entity's position, that can follow another
// entity. The highest priority camera is the one shown (see Cameras).
struct CameraComponent : public Component<CameraComponent> {
  COMPONENT_NAME("CameraComponent");

  struct Rect {
    glm::vec2 min{0.0f}, max{0.0f};
    bool contains(glm::vec2 p) const { return p.x >= min.x && p.y >= min.y && p.x <= max.x && p.y <= max.y; }
  };

  std::string target;               // name of the entity followed; "" (and no targetId): stays put
  EntityId targetId = kNoEntityId;  // the one followed: found by name, or set by a script
  glm::vec2 deadZone{0.0f};         // world units around the center the target moves in freely
  float smoothing = 8.0f;           // per second: higher catches up faster; 0 snaps
  float lookAhead = 0.0f;           // world units ahead of a target moving sideways
  std::optional<Rect> bounds;       // world rect the view stays inside (a level's edges)
  std::vector<Rect> regions;        // while the target is in one, the view stays inside it instead of bounds
  float zoom = 1.0f;                // above 1 zooms in
  float priority = 0.0f;            // the highest is shown
  float blend = 0.0f;               // seconds to ease over from the camera shown before

  // Following: where it follows to before looking ahead and keeping inside, how far
  // ahead it looks, the region it's in (-1: the bounds), and whether it's panning over to a new one.
  glm::vec2 base{0.0f};
  float ahead = 0.0f;
  int region = -1;
  bool panning = false;
  bool started = false;  // found its target (or was placed by a script)
};
