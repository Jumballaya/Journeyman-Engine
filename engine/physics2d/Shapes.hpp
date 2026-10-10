#pragma once

#include <optional>
#include <span>

#include <glm/glm.hpp>

// The collider shapes as geometry: an axis-aligned box or a circle. Touching
// edges don't count as overlapping, as in CollisionSystem.
struct Shape {
  enum class Kind { Box, Circle };
  Kind kind = Kind::Box;
  glm::vec2 center{0.0f};
  glm::vec2 half{0.0f};  // a box's half size
  float radius = 0.0f;   // a circle's

  static Shape box(glm::vec2 center, glm::vec2 half) { return {Kind::Box, center, half, 0.0f}; }
  static Shape circle(glm::vec2 center, float radius) { return {Kind::Circle, center, glm::vec2(0.0f), glm::max(radius, 0.0f)}; }
  // Half the size of the box around it.
  glm::vec2 extent() const { return kind == Kind::Circle ? glm::vec2(radius) : half; }
};

bool overlaps(const Shape& a, const Shape& b);

// Whether a, moving by aTravel this frame while b moved by bTravel, touched b
// at any moment of it (ending where they are now): a fast shape can't pass
// through a thin one between frames.
bool touchedDuring(const Shape& a, glm::vec2 aTravel, const Shape& b, glm::vec2 bTravel);

// The same along bent ways: each shape's offsets from where it is now at each
// turn, the last 0 (one 0: it stayed), gone at an even speed over the frame.
bool touchedAlongWays(const Shape& a, std::span<const glm::vec2> aWay, const Shape& b, std::span<const glm::vec2> bWay);

struct ShapeHit {
  float distance;    // along the ray, from its origin
  glm::vec2 normal;  // the surface's, facing the ray
};

// Where a ray from origin along direction (unit length) first enters the
// shape, within maxDistance (inclusive). A ray starting inside hits it at
// distance 0, facing back along the ray; one only grazing it misses.
std::optional<ShapeHit> raycast(const Shape& shape, glm::vec2 origin, glm::vec2 direction, float maxDistance);

// Terrain is made of segments (a to b): a ray (unit direction) crossing one
// from either side hits it, its normal facing the ray; a one-way segment (a
// platform to jump up through) only a ray heading down onto its top. A ray
// starting on a segment doesn't cross it.
std::optional<ShapeHit> raycastSegment(glm::vec2 a, glm::vec2 b, bool oneWay, glm::vec2 origin, glm::vec2 direction,
                                       float maxDistance);
// Whether a box or circle crosses the segment (touching it doesn't count).
bool overlapsSegment(const Shape& shape, glm::vec2 a, glm::vec2 b);

// How far apart two shapes are: 0 touching, below 0 overlapping (by that much).
float gapBetween(const Shape& a, const Shape& b);
// How far a shape is from the segment a-b: 0 when they touch or cross.
float gapToSegment(const Shape& shape, glm::vec2 a, glm::vec2 b);
