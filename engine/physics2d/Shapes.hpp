#pragma once

#include <optional>

#include <glm/glm.hpp>

// The collider shapes as geometry: an axis-aligned box, or a circle when
// radius > 0. Touching edges don't count as overlapping, as in CollisionSystem.
struct Shape {
  glm::vec2 center{0.0f};
  glm::vec2 half{0.0f};  // a box's half size; a circle's is (radius, radius)
  float radius = 0.0f;

  static Shape box(glm::vec2 center, glm::vec2 half) { return {center, half, 0.0f}; }
  static Shape circle(glm::vec2 center, float radius) { return {center, glm::vec2(radius), radius}; }
  bool isCircle() const { return radius > 0.0f; }
};

bool overlaps(const Shape& a, const Shape& b);

// Whether a, moving by aTravel this frame while b moved by bTravel, touched b
// at any moment of it (ending where they are now): a fast shape can't pass
// through a thin one between frames.
bool touchedDuring(const Shape& a, glm::vec2 aTravel, const Shape& b, glm::vec2 bTravel);

struct ShapeHit {
  float distance;    // along the ray, from its origin
  glm::vec2 normal;  // the surface's, facing the ray
};

// Where a ray from origin along direction (unit length) first enters the
// shape, within maxDistance. A ray starting inside hits it at distance 0,
// facing back along the ray.
std::optional<ShapeHit> raycast(const Shape& shape, glm::vec2 origin, glm::vec2 direction, float maxDistance);
